#!/usr/bin/env python
# coding=utf8

from __future__ import print_function
import argparse
import daemon
import eeml
import pulselib
import sys
import time

INTERVAL = 4
TIMEOUT = 30

class Pachube:
	class NoSuchFeedData(Exception):
		pass

	def __init__(self, db, feed, data):
		key = db.select1("SELECT key FROM pachube WHERE feed = %(feed)s AND data = %(data)s", { "feed": feed, "data": data })
		if key is None:
			raise self.NoSuchFeedData("{0}/{1}".format(feed, data))

		self.db = db
		self.feed = feed
		self.data = data
		self.key = key[0]
		self.log = pulselib.Log("pulsepachube/{0}/{1}".format(feed, data))

	def update(self, value, rate, log):
		ok = False
		status = None
		id = None
		err = []
		try:
			pac = eeml.Pachube("/api/feeds/{0}.xml".format(self.feed), self.key)
			pac.update([eeml.Data("{0}.value".format(self.data), value)])
			pac.update([eeml.Data("{0}.rate".format(self.data), rate)])
			pac.put()
		except Exception, e:
			err = [e]
		else:
			ok = True
		
		self.log("{0}".format(log))
		for msg in err:
			self.log("  {0}".format(msg))
		return ok

	def is_newer_update(self, ts):
		return self.db.update("UPDATE pachube SET lastupdate = %(ts)s WHERE feed = %(feed)s AND data = %(data)s AND (lastupdate IS NULL OR lastupdate < %(ts)s)", { "ts": ts, "feed": self.feed, "data": self.data }) == 1

	def get_last_update(self):
		data = self.db.select1("SELECT NOW(),lastupdate FROM pachube WHERE feed = %(feed)s AND data = %(data)s", { "feed": self.feed, "data": self.data })
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

class PulsePachube(pulselib.Handler):
	def __init__(self, db, meter, feed, data):
		pulselib.Handler.__init__(self, db, meter, TIMEOUT)
		self.pachube = Pachube(db, feed, data)

	def startup_delay(self):
		self.pachube.wait()

	def is_newer_update(self, ts):
		return self.pachube.is_newer_update(ts)

	def handle_pulse(self, ts, value, rate):
		return self.pachube.update(value, rate, "{0:09.3f} m³ ({1:04.2f} m³/hr) <{2}>".format(value, rate, ts))

	def pulse_delay(self, ok):
		if ok is not None:
			self.pachube.delay(ok)

if __name__ == "__main__":
	EXIT_SUCCESS, EXIT_FAILURE = range(0, 2)

	parser = argparse.ArgumentParser(description='Update pachube with gas meter pulses')
	parser.add_argument('-d', '--daemon', action='store_true', help='Run in the background')
	parser.add_argument('meter', help='Meter identifier')
	parser.add_argument('feed', help='Pachube feed')
	parser.add_argument('data', help='Pachube data stream prefix')
	args = parser.parse_args()

	db = pulselib.DB()
	pachube = PulsePachube(db, args.meter, args.feed, args.data)

	if args.daemon:
		with daemon.DaemonContext():
			pachube.main_loop()
	else:
		pachube.main_loop()

	sys.exit(EXIT_FAILURE)
