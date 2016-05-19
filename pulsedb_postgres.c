#include <sys/time.h>
#include <errno.h>
#include <postgresql/libpq-fe.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pulsedb.h"
#include "pulsedb_postgres.h"

#ifdef SYSLOG
# include <syslog.h>
#endif

#ifndef TABLE
# define TABLE "pulses"
#endif

PGconn *conn = NULL;
const char *meter;

void pulse_meter(const char *value) {
	char *end = NULL;

	errno = EINVAL;
	cerror("Meter value cannot be empty", value[0] == '\0');

	errno = 0;
	strtol(value, &end, 10);
	cerror(value, errno != 0);

	errno = EINVAL;
	cerror(value, end[0] != '\0');

	meter = value;
}

static bool db_connect(void) {
	PGresult *res = NULL;

	if (conn == NULL) {
		conn = PQconnectdb("");

		if (conn == NULL) {
			return false;
		} else {
			res = PQprepare(conn, "pulse_exists", "SELECT NULL FROM " TABLE " WHERE meter = $1 AND start = to_timestamp($2)", 2, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_on", "INSERT INTO " TABLE " (meter, start) VALUES($1, to_timestamp($2))", 2, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_off", "UPDATE " TABLE " SET stop = to_timestamp($3) WHERE meter = $1 AND start = to_timestamp($2)", 3, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_on_off", "INSERT INTO " TABLE " (meter, start, stop) VALUES($1, to_timestamp($2), to_timestamp($3))", 3, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_cancel", "DELETE FROM " TABLE " WHERE meter = $1 AND start = to_timestamp($2)", 2, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_resume", "UPDATE " TABLE " SET stop = NULL WHERE meter = $1 AND start = to_timestamp($2)", 2, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_reset_check", "SELECT NULL FROM (SELECT value FROM readings WHERE meter = $1 ORDER BY ts DESC LIMIT 1) last WHERE last.value IS NULL", 1, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_reset", "INSERT INTO readings (meter) VALUES($1)", 1, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = NULL;
		}
	}

	if (PQstatus(conn) != CONNECTION_OK)
		goto fail;

	return true;

fail:
	_printf("db_connect: %s", PQerrorMessage(conn));
	if (res != NULL)
		PQclear(res);
	PQfinish(conn);
	conn = NULL;
	return false;
}

static void db_disconnect(void) {
	if (conn != NULL) {
		PGresult *res;

		res = PQexec(conn, "DEALLOCATE PREPARE pulse_exists");
		PQclear(res);

		res = PQexec(conn, "DEALLOCATE PREPARE pulse_on");
		PQclear(res);

		res = PQexec(conn, "DEALLOCATE PREPARE pulse_off");
		PQclear(res);

		res = PQexec(conn, "DEALLOCATE PREPARE pulse_on_off");
		PQclear(res);

		res = PQexec(conn, "DEALLOCATE PREPARE pulse_cancel");
		PQclear(res);

		res = PQexec(conn, "DEALLOCATE PREPARE pulse_resume");
		PQclear(res);

		res = PQexec(conn, "DEALLOCATE PREPARE pulse_reset_check");
		PQclear(res);

		res = PQexec(conn, "DEALLOCATE PREPARE pulse_reset");
		PQclear(res);

		PQfinish(conn);
		conn = NULL;
	}
}

bool pulse_on(const struct timeval *on) {
	PGresult *res;
	char tmp[1][32];
	const char *param[2] = { meter, tmp[0] };
	bool done = false;

	if (!db_connect())
		return false;

	sprintf(tmp[0], "%lu.%06u", (unsigned long int)on->tv_sec, (unsigned int)on->tv_usec);

	res = PQexecPrepared(conn, "pulse_exists", 2, param, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		_printf("pulse_exists: %s", PQerrorMessage(conn));

		PQclear(res);
		db_disconnect();
		return false;
	} else {
		done = (PQntuples(res) > 0);

		PQclear(res);
	}

	if (done)
		return true;

	res = PQexecPrepared(conn, "pulse_on", 2, param, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		_printf("pulse_on: %s", PQerrorMessage(conn));

		PQclear(res);
		db_disconnect();
		return false;
	} else {
		PQclear(res);
		return true;
	}
}

bool pulse_off(const struct timeval *on, const struct timeval *off) {
	PGresult *res;
	char tmp[2][32];
	const char *param[3] = { meter, tmp[0], tmp[1] };

	if (!db_connect())
		return false;

	sprintf(tmp[0], "%lu.%06u", (unsigned long int)on->tv_sec, (unsigned int)on->tv_usec);
	sprintf(tmp[1], "%lu.%06u", (unsigned long int)off->tv_sec, (unsigned int)off->tv_usec);

	res = PQexecPrepared(conn, "pulse_off", 3, param, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		_printf("pulse_off: %s", PQerrorMessage(conn));

		PQclear(res);
		db_disconnect();
		return false;
	} else {
		PQclear(res);
		return true;
	}
}

bool pulse_on_off(const struct timeval *on, const struct timeval *off) {
	PGresult *res;
	char tmp[2][32];
	const char *param[3] = { meter, tmp[0], tmp[1] };
	bool done = false;

	if (!db_connect())
		return false;

	sprintf(tmp[0], "%lu.%06u", (unsigned long int)on->tv_sec, (unsigned int)on->tv_usec);
	sprintf(tmp[1], "%lu.%06u", (unsigned long int)off->tv_sec, (unsigned int)off->tv_usec);

	res = PQexecPrepared(conn, "pulse_off", 3, param, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		_printf("pulse_off: %s", PQerrorMessage(conn));

		PQclear(res);
		db_disconnect();
		return false;
	} else {
		done = strcmp("0", PQcmdTuples(res));

		PQclear(res);
	}

	if (done)
		return true;

	res = PQexecPrepared(conn, "pulse_on_off", 3, param, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		_printf("pulse_on_off: %s", PQerrorMessage(conn));

		PQclear(res);
		db_disconnect();
		return false;
	} else {
		PQclear(res);
		return true;
	}
}

bool pulse_cancel(const struct timeval *on) {
	PGresult *res;
	char tmp[1][32];
	const char *param[2] = { meter, tmp[0] };

	if (!db_connect())
		return false;

	sprintf(tmp[0], "%lu.%06u", (unsigned long int)on->tv_sec, (unsigned int)on->tv_usec);

	res = PQexecPrepared(conn, "pulse_cancel", 2, param, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		_printf("pulse_cancel: %s", PQerrorMessage(conn));

		PQclear(res);
		db_disconnect();
		return false;
	} else {
		PQclear(res);
		return true;
	}
}

bool pulse_resume(const struct timeval *on) {
	PGresult *res;
	char tmp[1][32];
	const char *param[2] = { meter, tmp[0] };

	if (!db_connect())
		return false;

	sprintf(tmp[0], "%lu.%06u", (unsigned long int)on->tv_sec, (unsigned int)on->tv_usec);

	res = PQexecPrepared(conn, "pulse_resume", 2, param, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		_printf("pulse_resume: %s", PQerrorMessage(conn));

		PQclear(res);
		db_disconnect();
		return false;
	} else {
		PQclear(res);
		return true;
	}
}

bool pulse_reset(void) {
	PGresult *res;
	const char *param[2] = { meter };
	bool done;

	if (!db_connect())
		return false;

	res = PQexecPrepared(conn, "pulse_reset_check", 1, param, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_TUPLES_OK) {
		_printf("pulse_reset_check: %s", PQerrorMessage(conn));

		PQclear(res);
		db_disconnect();
		return false;
	} else {
		done = (PQntuples(res) > 0);

		PQclear(res);
	}

	if (done)
		return true;

	res = PQexecPrepared(conn, "pulse_reset", 1, param, NULL, NULL, 0);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		_printf("pulse_reset: %s", PQerrorMessage(conn));

		PQclear(res);
		db_disconnect();
		return false;
	} else {
		PQclear(res);
		return true;
	}
}
