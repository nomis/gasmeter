#!/usr/bin/env python
# coding=utf8

from __future__ import division
from __future__ import print_function
import datetime
import json
import oauth2 as oauth
import pg
import pgdb
import select
import sys
import syslog
import time
import urllib

EXIT_SUCCESS, EXIT_FAILURE = range(0, 2)
INTERVAL = 30

def listen(db):
	c = db.cursor()
	c.execute("SET TIME ZONE 0")
	c.execute("LISTEN changed")
	c.close()
	db.commit()

def wait_notify(db):
	notify = db._cnx.getnotify()
	while notify is None:
		select.select([db._cnx], [], [db._cnx])
		notify = db._cnx.getnotify()
	return notify

def tso(ts):
	return datetime.datetime.strptime(ts, '%Y-%m-%d %H:%M:%S.%f+00')

def tsd(cur, prev):
	delta = tso(cur) - tso(prev)
	if sys.version_info < (2, 7):
		return (delta.microseconds + (delta.seconds + delta.days * 24 * 3600) * 10**6) / 10**6
	else:
		return delta.total_seconds()

def get_reading(db, meter):
	TS, VALUE = range(0, 2)
	CUR, PREV = range(0, 2)
	c = db.cursor()
	c.execute("SELECT ts,value FROM abs_pulses WHERE meter = %(id)s ORDER BY ts DESC LIMIT 2", { "id": meter })
	data = c.fetchall()
	c.close()
	if len(data) != 2 or data[PREV][VALUE] is None or data[CUR][VALUE] is None:
		return None
	return {
		"ts": data[CUR][TS],
		"value": data[CUR][VALUE],
		"delta": tsd(data[CUR][TS], data[PREV][TS]),
		"step": data[CUR][VALUE] - data[PREV][VALUE]
	}

def tweet_update(db, account, ts):
	ok = False
	c = db.cursor()
	c.execute("UPDATE twitter_accounts SET lastupdate = %(ts)s WHERE name = %(account)s AND (lastupdate IS NULL OR lastupdate < %(ts)s)", { "ts": ts, "account": account })
	if c.rowcount == 1:
		ok = True
	c.close()
	return ok

def last_update(db, account):
	c = db.cursor()
	c.execute("SELECT NOW(),lastupdate FROM twitter_accounts WHERE name = %(account)s", { "account": account })
	data = c.fetchone()
	c.close()
	if data is None or data[1] is None:
		return None
	return max(0, tsd(data[0], data[1]))

def get_oauth_config(db, account):
	CON_KEY, CON_SEC, ACC_TOK, ACC_SEC = range(0, 4)
	c = db.cursor()
	c.execute("SELECT o.key,o.secret,a.token,a.secret FROM twitter_oauth o, twitter_accounts a WHERE a.name = %(account)s AND o.name = a.key", { "account": account })
	data = c.fetchone()
	c.close()
	if data is None:
		return None
	return (oauth.Consumer(data[CON_KEY], data[CON_SEC]), oauth.Token(data[ACC_TOK], data[ACC_SEC]))

if len(sys.argv) != 3:
	print("Usage: pulsetweet <meter> <account>")
	sys.exit(EXIT_FAILURE)

meter, account = sys.argv[1:]

db = pgdb.connect()
listen(db)

consumer, token = get_oauth_config(db, account)
client = oauth.Client(consumer, token)

syslog.openlog("pulsetweet/{0}".format(account))

last = last_update(db, account)
if last is not None and last < INTERVAL:
	print("Waiting {0}s".format(INTERVAL - last))
	time.sleep(INTERVAL - last)

while True:
	print("Getting reading...")
	reading = get_reading(db, meter)
	print("Reading: {0}".format(reading))
	if reading is not None:
		if tweet_update(db, account, reading["ts"]):
			tweet = "{0:08.2f} m³ ({1:04.2f} m³/hr)".format(reading["value"], float(reading["step"]) / reading["delta"] * 3600)
			print("[{0}] {1}".format(reading["ts"], tweet))

			resp, content = client.request("https://twitter.com/statuses/update.json", "POST", "status=" + urllib.quote(tweet))
			status = resp["status"] if resp is not None and "status" in resp else None
			print("Status {0}".format(status))

			data = json.loads(content)
			id = data["id"] if data is not None and "id" in data else None
			print("ID {0}".format(id))

			syslog.syslog("{0} {1} [{2}] {3}".format(tweet, reading["ts"], status, id))

			if status == "200":
				db.commit()
			else:
				db.rollback()

			print("Waiting {0}s".format(INTERVAL))
			time.sleep(INTERVAL)
		else:
			db.commit()
	else:
		db.commit()
	wait_notify(db)
