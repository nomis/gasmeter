#!/usr/bin/env python2
# coding=utf8

from __future__ import division
from __future__ import print_function
import datetime
import pg
import pgdb
import select
import sys
import syslog
import time

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
		if isinstance(cur, str):
			cur = self.tso(cur)
		if isinstance(prev, str):
			prev = self.tso(prev)
		delta = cur - prev
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
				"step": data[CUR][VALUE] - data[PREV][VALUE],
				"idle": self.tsd(datetime.datetime.utcnow(), data[CUR][TS])
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

	def wait(self, timeout=0):
		if not self.connect():
			raise self.Reconnect
		try:
			notify = self.db._cnx.getnotify()
			if notify is None:
				print("Listening...")
			if timeout == 0:
				timeout = None

			while notify is None:
				if self.db._cnx.fileno() < 0:
					raise self.Reconnect
				(r, w, x) = select.select([self.db._cnx], [], [self.db._cnx], timeout)
				if len(r) == 0 and len(w) == 0 and len(x) == 0:
					print("Timeout")
					return
				notify = self.db._cnx.getnotify()
			print("Notified")
		except self.Reconnect:
			self.abort()
			raise self.Reconnect
		except pg.DatabaseError, e:
			self.abort(e)
			raise self.Reconnect

class Log:
	def __init__(self, name):
		syslog.openlog(name)

	def __call__(self, message):
		syslog.syslog(message)

class Handler:
	def __init__(self, db, meter, timeout=0):
		self.db = db
		self.pulses = Pulses(db, meter)
		self.timeout = timeout
		self.last_rate = ""

	def startup_delay(self):
		pass

	def is_newer_update(self, ts):
		return False

	def handle_pulse(self, ts, value, rate):
		pass

	def process_reading(self):
		reading = self.pulses.get_reading()
		if reading is None:
			self.db.commit()
			return None
		elif self.is_newer_update(reading["ts"]):
			(ts, value, rate) = (reading["ts"], reading["value"], float(reading["step"]) / reading["delta"] * 3600)
			print("[{0}] {1:09.3f} m³ ({2:04.2f} m³/hr)".format(ts, value, rate))

			ok = self.handle_pulse(ts, value, rate)
			try:
				if ok:
					self.db.commit()
					self.last_rate = "{0:04.2f}".format(rate)
				else:
					self.db.rollback()
			except DB.Reconnect:
				ok = False

			return ok
		elif self.timeout > 0 and reading["idle"] > self.timeout:
			(ts, value, rate, idle_rate) = (reading["ts"], reading["value"], float(reading["step"]) / reading["delta"] * 3600, float(reading["value"] + reading["step"]) / reading["idle"])
			if idle_rate < rate:
				fake_rate = "{0:04.2f}".format(idle_rate)
				if fake_rate != self.last_rate:
					idle_rate = float(fake_rate)
					print("[{0}] {1:09.3f} m³ ({2:04.2f} m³/hr)".format(ts, value, idle_rate))

					ok = self.handle_pulse(ts, value, idle_rate)
					try:
						if ok:
							self.last_rate = fake_rate
							self.db.commit()
						else:
							self.db.rollback()
					except DB.Reconnect:
						ok = False

					return ok

			self.db.commit()
			return None
		else:
			self.db.commit()
			return None

	def pulse_delay(self, ok):
		pass

	def wait_for_change(self):
		timeout = self.timeout

		# Getting down to a rate of "0.00" and disabling the early timeout will take a long time
		special = {
			"0.00": 0,
			"0.01": 100,
			"0.02": 10
		}
		if self.last_rate in special:
			timeout = timeout * special[self.last_rate]

		self.db.wait(timeout)

	def main_loop(self):
		while True:
			try:
				ok = self.process_reading()
				self.pulse_delay(ok)
				if ok != False:
					self.wait_for_change()
				# allow some time for invalid readings to be reverted
				time.sleep(2)
			except DB.Reconnect:
				time.sleep(5)
				while not self.db.connect():
					time.sleep(5)
