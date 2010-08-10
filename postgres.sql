CREATE FUNCTION last_reading_ts(meter integer, before timestamp with time zone) RETURNS timestamp with time zone
    AS $_$SELECT ts FROM readings WHERE meter = $1 AND ts < $2 ORDER BY ts DESC LIMIT 1;$_$
    LANGUAGE sql STABLE STRICT;

CREATE FUNCTION last_reading_value(meter integer, before timestamp with time zone) RETURNS numeric
    AS $_$SELECT value FROM readings WHERE meter = $1 and ts < $2 ORDER BY ts DESC LIMIT 1;$_$
    LANGUAGE sql STABLE STRICT;

CREATE FUNCTION meter_pulse_interval(meter integer) RETURNS numeric
    AS $_$SELECT pulse FROM meters WHERE id = $1;$_$
    LANGUAGE sql STABLE STRICT;

CREATE FUNCTION meter_pulse_offset(meter integer) RETURNS numeric
    LANGUAGE sql STABLE STRICT
    AS $_$SELECT "offset" FROM meters WHERE id = $1;$_$;

CREATE FUNCTION pulse_calculate(meter integer, before timestamp with time zone) RETURNS numeric
    AS $_$SELECT reading_calculate($1, $2, pulse_count($1, $2));$_$
    LANGUAGE sql STABLE STRICT;

CREATE FUNCTION pulse_count(meter integer, before timestamp with time zone) RETURNS bigint
    AS $_$SELECT COUNT(*) FROM pulses WHERE meter = $1 AND start > last_reading_ts($1, $2) AND start <= $2;$_$
    LANGUAGE sql STABLE STRICT;

CREATE FUNCTION reading_calculate(meter integer, before timestamp with time zone, pulses bigint) RETURNS numeric
    AS $_$SELECT reading_floor($1, last_reading_value($1, $2)) + $3 * meter_pulse_interval($1);$_$
    LANGUAGE sql STABLE STRICT;

CREATE FUNCTION reading_floor(meter integer, value numeric) RETURNS numeric
    AS $_$SELECT $2 - MOD($2 - meter_pulse_offset($1), meter_pulse_interval($1));$_$
    LANGUAGE sql STABLE STRICT;

CREATE TABLE pulses (
    meter integer NOT NULL,
    start timestamp with time zone NOT NULL,
    stop timestamp with time zone,
    CONSTRAINT valid_pulse CHECK ((stop >= start))
);

CREATE VIEW abs_pulses AS
    SELECT pulses.meter, pulses.start AS ts, pulse_calculate(pulses.meter, pulses.start) AS value, (pulses.stop - pulses.start) AS pulse FROM pulses;

CREATE TABLE meters (
    id serial NOT NULL,
    name text NOT NULL,
    pulse numeric(9,4),
    "offset" numeric(9,4)
);

CREATE TABLE readings (
    meter integer NOT NULL,
    ts timestamp with time zone DEFAULT now() NOT NULL,
    value numeric(9,4)
);

CREATE TABLE twitter_accounts (
    name text NOT NULL,
    key text NOT NULL,
    token text NOT NULL,
    secret text NOT NULL,
    lastupdate timestamp with time zone
);

CREATE TABLE twitter_oauth (
    name text NOT NULL,
    key text NOT NULL,
    secret text NOT NULL
);

CREATE TABLE pachube (
    feed bigint NOT NULL,
    data text NOT NULL,
    key text NOT NULL,
    lastupdate timestamp with time zone
);

ALTER TABLE ONLY meters
    ADD CONSTRAINT meters_name_key UNIQUE (name);

ALTER TABLE ONLY meters
    ADD CONSTRAINT meters_pkey PRIMARY KEY (id);

ALTER TABLE ONLY pulses
    ADD CONSTRAINT pulses_pkey PRIMARY KEY (meter, start);

ALTER TABLE ONLY readings
    ADD CONSTRAINT readings_pkey PRIMARY KEY (meter, ts);

ALTER TABLE ONLY twitter_accounts
    ADD CONSTRAINT twitter_accounts_pkey PRIMARY KEY (name);

ALTER TABLE ONLY twitter_oauth
    ADD CONSTRAINT twitter_oauth_pkey PRIMARY KEY (name);

CREATE RULE notify_delete AS ON DELETE TO pulses DO NOTIFY changed;

CREATE RULE notify_delete AS ON DELETE TO readings DO NOTIFY changed;

CREATE RULE notify_insert AS ON INSERT TO pulses DO NOTIFY changed;

CREATE RULE notify_insert AS ON INSERT TO readings DO NOTIFY changed;

CREATE RULE notify_update AS ON UPDATE TO pulses DO NOTIFY changed;

CREATE RULE notify_update AS ON UPDATE TO readings DO NOTIFY changed;

ALTER TABLE ONLY pulses
    ADD CONSTRAINT pulses_meter_fkey FOREIGN KEY (meter) REFERENCES meters(id);

ALTER TABLE ONLY readings
    ADD CONSTRAINT readings_meter_fkey FOREIGN KEY (meter) REFERENCES meters(id);

ALTER TABLE ONLY twitter_accounts
    ADD CONSTRAINT twitter_accounts_key_fkey FOREIGN KEY (key) REFERENCES twitter_oauth(name);

ALTER TABLE ONLY pachube
    ADD CONSTRAINT pachube_pkey PRIMARY KEY (feed, data);

