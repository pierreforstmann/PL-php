-- plphp--1.0.sql

CREATE FUNCTION plphp_call_handler()
RETURNS language_handler
AS 'MODULE_PATHNAME'
LANGUAGE C;

CREATE LANGUAGE plphp
    HANDLER plphp_call_handler;

