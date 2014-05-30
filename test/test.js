QUnit.module('i-json');

var ijson = require('..');

function parse(data, options) {
	var parser = ijson.createParser();
	parser.update(data);
	return parser.result();
}

test("basic values", 15, function() {
	strictEqual(parse('null'), null);
	strictEqual(parse('true'), true);
	strictEqual(parse('false'), false);
	strictEqual(parse('"null"'), "null");
	strictEqual(parse('""'), "");
	strictEqual(parse(''), undefined);
	strictEqual(parse('5'), 5);
	strictEqual(parse('-5'), -5);
	strictEqual(parse('3.14'), 3.14);
	strictEqual(parse('-3.14'), -3.14);
	strictEqual(parse('6.02e23'), 6.02e23);
	strictEqual(parse('-6.02e+23'), -6.02e23);
	strictEqual(parse('6.62E-34'), 6.62e-34);
	deepEqual(parse('{}'), {});
	deepEqual(parse('[]'), []);
});

test("complex values", 1, function() {
	deepEqual(parse('{"a":{"b":{"c":1,"d":2},"e":[3,4],"f":[true,false,null]}}'), 
		{"a":{"b":{"c":1,"d":2},"e":[3,4],"f":[true,false,null]}});
});

test("extra spaces", 5, function() {
	strictEqual(parse(' null '), null);
	strictEqual(parse(' 5 '), 5);
	strictEqual(parse(' "" '), "");
	strictEqual(parse('  '), undefined);
	deepEqual(parse(' { "a": {"b" :{"c":1,"d":2 },"e" : [ 3 ,4 ],"f":[ true , false, null]}}'), 
		{"a":{"b":{"c":1,"d":2},"e":[3,4],"f":[true,false,null]}});
});

test("escape sequences", 9, function() {
	strictEqual(parse('"\\n"'), "\n");
	strictEqual(parse('"\\r"'), "\r");
	strictEqual(parse('"\\t"'), "\t");
	strictEqual(parse('"\\""'), "\"");
	strictEqual(parse('"\\\\"'), "\\");
	strictEqual(parse('"\\u0040"'), "@");
	strictEqual(parse('"\\u00e9"'), "é");
	strictEqual(parse('"\\u20AC"'), "€");
	strictEqual(parse('"a\\r\\nbc\\td\\"\\\\e\\u20ACf"'), "a\r\nbc\td\"\\e€f");
});