#!/usr/bin/env python
# coding=utf8

from __future__ import division
from __future__ import print_function
import argparse
import datetime
import daemon
import json
import oauth2 as oauth
import pg
import pgdb
import select
import sys
import syslog
import time
import urllib

class Pulses:
	class NoSuchMeter(Exception):
		pass

	def __init__(self, db, meter):
		data = db.select1("SELECT id FROM meters WHERE id = %(id)s", { "id": meter })
		if data is None:
			raise self.NoSuchMeter(meter)

		self.meter = meter
		self.db = db

	def tso(self, ts):
		return datetime.datetime.strptime(ts, '%Y-%m-%d %H:%M:%S.%f+00')

	def tsd(self, cur, prev):
		delta = self.tso(cur) - self.tso(prev)
		if sys.version_info < (2, 7):
			return (delta.microseconds + (delta.seconds + delta.days * 24 * 3600) * 10**6) / 10**6
		else:
			return delta.total_seconds()

	def get_reading(self):
		TS, VALUE = range(0, 2)
		CUR, PREV = range(0, 2)

		print("Getting reading...")
		data = self.db.select("SELECT ts,value FROM abs_pulses WHERE meter = %(id)s ORDER BY ts DESC LIMIT 2", { "id": self.meter })

		reading = None
		if len(data) == 2 and data[PREV][VALUE] is not None and data[CUR][VALUE] is not None:
			reading = {
				"ts": data[CUR][TS],
				"value": data[CUR][VALUE],
				"delta": self.tsd(data[CUR][TS], data[PREV][TS]),
				"step": data[CUR][VALUE] - data[PREV][VALUE]
			}

		print("Reading: {0}".format(reading))
		return reading

class DB:
	class Reconnect(Exception):
		pass

	def __init__(self):
		self.db = None

	def abort(self, e=None):
		if e is not None:
			print(e, file=sys.stderr)
		if self.db is not None:
			try:
				self.db.close()
			except pg.DatabaseError:
				pass
		self.db = None

	def connect(self):
		try:
			if self.db is None:
				print("Connecting to DB...")
				self.db = pgdb.connect()
				self.listen()
				self.commit()
		except pg.DatabaseError, e:
			self.abort(e)
			return False
		except self.Reconnect:
			return False
		else:
			return True

	def listen(self):
		try:
			c = self.db.cursor()
			c.execute("SET TIME ZONE 0")
			c.execute("LISTEN changed")
			c.close()
		except pg.DatabaseError, e:
			self.abort(e)
			raise self.Reconnect

	def select(self, query, data):
		if not self.connect():
			raise self.Reconnect
		try:
			c = self.db.cursor()
			c.execute(query, data)
			data = c.fetchall()
			c.close()
		except pg.DatabaseError, e:
			self.abort(e)
			raise self.Reconnect
		else:
			return data

	def select1(self, query, data):
		data = self.select(query, data)
		return data[0] if len(data) > 0 else None

	def update(self, query, data):
		if not self.connect():
			raise self.Reconnect
		try:
			c = self.db.cursor()
			c.execute(query, data)
			rows = c.rowcount
			c.close()
		except pg.DatabaseError, e:
			self.abort(e)
			raise self.Reconnect
		else:
			return rows

	def commit(self):
		if self.db is None:
			raise self.Reconnect
		try:
			self.db.commit()
		except pg.DatabaseError, e:
			self.abort(e)
			raise self.Reconnect

	def rollback(self):
		if self.db is None:
			raise self.Reconnect
		try:
			self.db.rollback()
		except pg.DatabaseError, e:
			self.abort(e)
			raise self.Reconnect

	def wait(self):
		if not self.connect():
			raise self.Reconnect
		try:
			notify = self.db._cnx.getnotify()
			if notify is None:
				print("Listening...")

			while notify is None:
				if self.db._cnx.fileno() < 0:
					raise self.Reconnect
				select.select([self.db._cnx], [], [self.db._cnx])
				notify = self.db._cnx.getnotify()
			print("Notified")
		except self.Reconnect:
			self.abort()
			raise self.Reconnect
		except pg.DatabaseError, e:
			self.abort(e)
			raise self.Reconnect

class Log:
	def __init__(self, twitter):
		syslog.openlog("pulsetweet/{0}".format(twitter.account))

	def __call__(self, message):
		syslog.syslog(message)

class Twitter:
	class NoSuchAccount(Exception):
		pass

	def __init__(self, db, account):
		CON_KEY, CON_SEC, ACC_TOK, ACC_SEC = range(0, 4)

		data = db.select1("SELECT o.key,o.secret,a.token,a.secret FROM twitter_oauth o, twitter_accounts a WHERE a.name = %(account)s AND o.name = a.key", { "account": account })
		if data is None:
			raise self.NoSuchAccount(account)

		self.account = account
		self.client = oauth.Client(oauth.Consumer(data[CON_KEY], data[CON_SEC]), oauth.Token(data[ACC_TOK], data[ACC_SEC]))
		self.log = Log(self)
		self.db = db

	def tweet(self, message, log=None):
		ok = False
		status = None
		id = None
		err = []
		try:
			resp, content = self.client.request("https://twitter.com/statuses/update.json", "POST", "status=" + urllib.quote(message))
		except Exception, e:
			err = [e]
		else:
			if resp is not None and "status" in resp:
				status = resp["status"]
			print("Status {0}".format(status))
		
			if status == "200":
				ok = True
			else:
				err = [resp, content]
		
			try:
				data = json.loads(content)
			except Exception, e:
				err = [resp, content, e]
			else:
				if data is not None:
					if "id" in data:
						id = data["id"]

					if not ok and "error" in data and data["error"] == "Status is a duplicate.":
						ok = True
						err = [content]
		
		if log is None:
			log = tweet
		self.log("{0} [{1}] {2}".format(log, status, id))
		for msg in err:
			self.log("  {0}".format(msg))
		return ok

	def is_newer_update(self, ts):
		return self.db.update("UPDATE twitter_accounts SET lastupdate = %(ts)s WHERE name = %(account)s AND (lastupdate IS NULL OR lastupdate < %(ts)s)", { "ts": ts, "account": self.account }) == 1

	def get_last_update(self):
		data = self.db.select1("SELECT NOW(),lastupdate FROM twitter_accounts WHERE name = %(account)s", { "account": self.account })
		if data is None or data[1] is None:
			return None
		return max(0, tsd(data[0], data[1]))

	def wait(self):
		last = self.get_last_update()
		if last is not None and last < INTERVAL:
			print("Waiting {0}s".format(INTERVAL - last))
			time.sleep(INTERVAL - last)

	def delay(self, ok):
		secs = 30 if ok else 300
		print("Waiting {0}s".format(secs))
		time.sleep(secs)

class PulseTweeter:
	def __init__(self, db, meter, account):
		self.db = db
		self.pulses = Pulses(db, meter)
		self.twitter = Twitter(db, account)

	def startup_delay(self):
		self.twitter.wait()

	def process_reading(self):
		reading = self.pulses.get_reading()
		if reading is not None and self.twitter.is_newer_update(reading["ts"]):
			tweet = "{0:09.3f} m³ ({1:04.2f} m³/hr)".format(reading["value"], float(reading["step"]) / reading["delta"] * 3600)
			print("[{0}] {1}".format(reading["ts"], tweet))
	
			ok = self.twitter.tweet(tweet, "{0} <{1}>".format(tweet, reading["ts"]))
			try:
				if ok:
					self.db.commit()
				else:
					self.db.rollback()
			except DB.Reconnect:
				ok = False

			return ok
		else:
			self.db.commit()
			return None

	def tweet_delay(self, ok):
		if ok is not None:
			self.twitter.delay(ok)

	def wait_for_change(self):
		self.db.wait()

	def main_loop(self):
		while True:
			try:
				ok = self.process_reading()
				self.tweet_delay(ok)
				if ok != False:
					self.wait_for_change()
				# allow some time for invalid readings to be reverted
				time.sleep(2)
			except DB.Reconnect:
				while not self.db.connect():
					time.sleep(5)

if __name__ == "__main__":
	EXIT_SUCCESS, EXIT_FAILURE = range(0, 2)

	parser = argparse.ArgumentParser(description='Tweet gas meter pulses')
	parser.add_argument('-d', '--daemon', action='store_true', help='Run in the background')
	parser.add_argument('meter', help='Meter identifier')
	parser.add_argument('account', help='Twitter account')
	args = parser.parse_args()

	db = DB()
	tweeter = PulseTweeter(db, args.meter, args.account)

	if args.daemon:
		with daemon.DaemonContext():
			tweeter.main_loop()
	else:
		tweeter.main_loop()

	sys.exit(EXIT_FAILURE)
