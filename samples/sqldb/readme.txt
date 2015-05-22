This sample used MySQL 5,
If you want to use other database server,
change the following SQL script and query strings in ini file, properly.

-- MySQL Script Begin

USE test;

DROP TABLE IF EXISTS sc_sqldemo;
CREATE TABLE sc_sqldemo
(
    sn          INTEGER         NOT NULL AUTO_INCREMENT,
    uniqueid    BIGINT          NOT NULL,
    power       SMALLINT        NOT NULL,
    price       DECIMAL(6,2)    NOT NULL,
    cretime     DATETIME        NOT NULL,
    name        VARCHAR(80)     NOT NULL,
    
    UNIQUE INDEX (uniqueid),
    PRIMARY KEY (sn)
);

-- insert sample data
INSERT INTO sc_sqldemo VALUES(1, 1, 99, 123.45, now(), 'NameABC');




DELIMITER |

-- Return values: 0=not-found, 1=not-processed, 2=processed
DROP PROCEDURE IF EXISTS SC_Proc1|
CREATE PROCEDURE SC_Proc1
(
	_sn			INTEGER,
	_lowpower	SMALLINT,
	_addprice	DECIMAL(6,2)
)
BEGIN
	DECLARE _oldpower SMALLINT;

	SELECT power INTO _oldpower FROM sc_sqldemo WHERE sn=_sn;
	IF _oldpower > 0 THEN
		IF _oldpower < _lowpower THEN
			UPDATE sc_sqldemo SET price=price+_addprice, cretime=now() WHERE sn=_sn;
			SELECT 2 AS result;
		ELSE
			SELECT 1 AS result;
		END IF;
	ELSE
		SELECT 0 AS result;
	END IF;
END|