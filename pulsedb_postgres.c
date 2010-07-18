#include <sys/time.h>
#include <libpq-fe.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "pulsedb.h"

PGconn *conn = NULL;
char param_meter[32];

void pulse_meter(unsigned long int meter) {
	int ret;

	ret = sprintf(param_meter, "%lu", meter);
	cerror("sprintf", ret < 0);
}

static bool db_connect(void) {
	PGresult *res = NULL;

	if (conn == NULL) {
		conn = PQconnectdb("");

		if (conn == NULL) {
			return false;
		} else {
			res = PQprepare(conn, "pulse_exists", "SELECT NULL FROM pulses WHERE meter = $1 AND start = to_timestamp($2)", 2, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_on", "INSERT INTO pulses (meter, start) VALUES($1, to_timestamp($2))", 2, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_off", "UPDATE pulses SET stop = to_timestamp($3) WHERE meter = $1 and start = to_timestamp($2)", 3, NULL);
			if (PQresultStatus(res) != PGRES_COMMAND_OK) goto fail;
			PQclear(res);

			res = PQprepare(conn, "pulse_on_off", "INSERT INTO pulses (meter, start, stop) VALUES($1, to_timestamp($2), to_timestamp($3))", 3, NULL);
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

		PQfinish(conn);
		conn = NULL;
	}
}

bool pulse_on(const struct timeval *on) {
	PGresult *res;
	char tmp[1][32];
	const char *param[2] = { param_meter, tmp[0] };
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
	const char *param[3] = { param_meter, tmp[0], tmp[1] };

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
	const char *param[3] = { param_meter, tmp[0], tmp[1] };
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
