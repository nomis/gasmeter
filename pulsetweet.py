#!/usr/bin/env python
# coding=utf8

from __future__ import print_function
import argparse
import daemon
import json
import oauth2 as oauth
import pulselib
import sys
import time
import urllib

INTERVAL = 30

class Twitter:
	class NoSuchAccount(Exception):
		pass

	def __init__(self, db, account):
		CON_KEY, CON_SEC, ACC_TOK, ACC_SEC = range(0, 4)

		data = db.select1("SELECT o.key,o.secret,a.token,a.secret FROM twitter_oauth o, twitter_accounts a WHERE a.name = %(account)s AND o.name = a.key", { "account": account })
		if data is None:
			raise self.NoSuchAccount(account)

		self.db = db
		self.account = account
		self.client = oauth.Client(oauth.Consumer(data[CON_KEY], data[CON_SEC]), oauth.Token(data[ACC_TOK], data[ACC_SEC]))
		self.log = pulselib.Log("pulsetweet/{0}".format(account))

	def tweet(self, message, log=None):
		ok = False
		status = None
		id = None
		err = []
		try:
			resp, content = self.client.request("https://api.twitter.com/1/statuses/update.json", "POST", "status=" + urllib.quote(message))
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
		secs = INTERVAL if ok else INTERVAL * 10
		print("Waiting {0}s".format(secs))
		time.sleep(secs)

class PulseTweeter(pulselib.Handler):
	def __init__(self, db, meter, account):
		pulselib.Handler.__init__(self, db, meter)
		self.twitter = Twitter(db, account)

	def startup_delay(self):
		self.twitter.wait()

	def is_newer_update(self, ts):
		return self.twitter.is_newer_update(ts)

	def handle_pulse(self, ts, value, rate):
		tweet = "{0:08.2f} m³ ({1:04.2f} m³/hr)".format(value, rate)
		return self.twitter.tweet(tweet, "{0} <{1}>".format(tweet, ts))

	def pulse_delay(self, ok):
		if ok is not None:
			self.twitter.delay(ok)

if __name__ == "__main__":
	EXIT_SUCCESS, EXIT_FAILURE = range(0, 2)

	parser = argparse.ArgumentParser(description='Tweet gas meter pulses')
	parser.add_argument('-d', '--daemon', action='store_true', help='Run in the background')
	parser.add_argument('meter', help='Meter identifier')
	parser.add_argument('account', help='Twitter account')
	args = parser.parse_args()

	db = pulselib.DB()
	tweeter = PulseTweeter(db, args.meter, args.account)

	if args.daemon:
		with daemon.DaemonContext():
			tweeter.main_loop()
	else:
		tweeter.main_loop()

	sys.exit(EXIT_FAILURE)
