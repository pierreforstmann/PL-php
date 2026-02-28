--
-- Base functionality.
--
create extension plphp;

-- Basic things: scalars and arrays.
CREATE FUNCTION test_void() RETURNS integer
LANGUAGE plphp AS $$
	return;
$$;
SELECT test_void();

CREATE FUNCTION test_an_int() RETURNS integer
LANGUAGE plphp AS $$
	return 1;
$$;
SELECT test_an_int();
SELECT * FROM test_an_int();

CREATE FUNCTION test_an_array() RETURNS int[]
LANGUAGE plphp AS $$
	return array(1);
$$;
SELECT test_an_array();
SELECT * FROM test_an_array();

CREATE FUNCTION test_a_bogus_array() RETURNS int[]
LANGUAGE plphp AS $$
	return 1;
$$;
SELECT test_a_bogus_array();
SELECT * FROM test_a_bogus_array();

CREATE FUNCTION test_a_bogus_int() RETURNS integer
LANGUAGE plphp AS $$
	return array(1);
$$;
SELECT test_a_bogus_int();
SELECT * FROM test_a_bogus_int();

CREATE FUNCTION test_ndim_array(int, int) RETURNS int[]
LANGUAGE plphp AS $$
    if (!function_exists('bar')) {
    function bar($a, $b) {
        if ($a == 1) {
            return array($b, $b+1);
        }
        return array(bar($a-1, $b), bar($a-1, $b+1));
    }
    }

    $return = bar($args[0], $args[1]);
    return $return;
$$;
