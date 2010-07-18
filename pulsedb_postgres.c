#include <sys/time.h>
#include <libpq-fe.h>
#include <stdbool.h>

#include "pulsedb.h"

PGconn *conn = NULL;

static bool db_connect(void) {
	PGresult *res = NULL;

	if (conn == NULL) {
		conn = PQconnectdb("");

		if (conn == NULL) {
			return false;
		} else {
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

bool pulse_on(unsigned long int meter, const struct timeval *on) {
	PGresult *res;
	char tmp[2][32];
	const char *param[2] = { tmp[0], tmp[1] };

	if (!db_connect())
		return false;

	sprintf(tmp[0], "%lu", meter);
	sprintf(tmp[1], "%lu.%06u", (unsigned long int)on->tv_sec, (unsigned int)on->tv_usec);

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

bool pulse_off(unsigned long int meter, const struct timeval *on, const struct timeval *off) {
	PGresult *res;
	char tmp[3][32];
	const char *param[3] = { tmp[0], tmp[1], tmp[2] };

	if (!db_connect())
		return false;

	sprintf(tmp[0], "%lu", meter);
	sprintf(tmp[1], "%lu.%06u", (unsigned long int)on->tv_sec, (unsigned int)on->tv_usec);
	sprintf(tmp[2], "%lu.%06u", (unsigned long int)off->tv_sec, (unsigned int)off->tv_usec);

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

bool pulse_on_off(unsigned long int meter, const struct timeval *on, const struct timeval *off) {
	PGresult *res;
	char tmp[3][32];
	const char *param[3] = { tmp[0], tmp[1], tmp[2] };

	if (!db_connect())
		return false;

	sprintf(tmp[0], "%lu", meter);
	sprintf(tmp[1], "%lu.%06u", (unsigned long int)on->tv_sec, (unsigned int)on->tv_usec);
	sprintf(tmp[2], "%lu.%06u", (unsigned long int)off->tv_sec, (unsigned int)off->tv_usec);

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
