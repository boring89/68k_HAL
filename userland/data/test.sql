-- A tour of SQLite for the port self test; the output must be the same on 68k_HAL and on a Linux host.
.bail on
.headers on
.mode column

CREATE TABLE city (id INTEGER PRIMARY KEY, name TEXT NOT NULL, country TEXT, population INTEGER);
INSERT INTO city (name, country, population) VALUES
  ('Taipei', 'Taiwan', 2600000), ('Tokyo', 'Japan', 13900000), ('Osaka', 'Japan', 2700000),
  ('Kaohsiung', 'Taiwan', 2700000), ('Seoul', 'Korea', 9700000), ('Busan', 'Korea', 3400000);
CREATE INDEX city_country ON city (country);

SELECT country, count(*) AS cities, sum(population) AS people FROM city GROUP BY country ORDER BY people DESC;
SELECT name FROM city WHERE population > 3000000 ORDER BY name;

-- many rows: a recursive query fills a table, then it is read back
CREATE TABLE numbers (n INTEGER PRIMARY KEY, square INTEGER, text TEXT);
WITH RECURSIVE count(x) AS (SELECT 1 UNION ALL SELECT x + 1 FROM count WHERE x < 5000)
  INSERT INTO numbers SELECT x, x * x, printf('%05d-%s', x, hex(randomblob(2)) IS NOT NULL) FROM count;
SELECT count(*), sum(square), max(n), min(text) FROM numbers;
SELECT n, square FROM numbers WHERE n IN (1, 100, 2500, 5000) ORDER BY n;

-- a transaction that is rolled back leaves no trace
BEGIN;
DELETE FROM numbers;
SELECT count(*) AS inside_transaction FROM numbers;
ROLLBACK;
SELECT count(*) AS after_rollback FROM numbers;

-- window functions, JSON, strings, integers
SELECT name, population, rank() OVER (ORDER BY population DESC) AS place FROM city ORDER BY place, name;
SELECT json_extract('{"a": [1, 2, {"b": "deep"}]}', '$.a[2].b') AS j, upper('taipei') AS u, 7 / 2 AS idiv, 7 / 2.0 AS fdiv, 9223372036854775807 AS big;

-- the database is a real file: check it, and look at the file itself
PRAGMA integrity_check;
PRAGMA page_count;
VACUUM;
PRAGMA integrity_check;
