QUnit.module('i-json');

var ijson = require('..');

function parse(data, options) {
	var parser = ijson.createParser();
	parser.update(data);
	return parser.result();
}

test("basic values", 10, function() {
	strictEqual(parse('null'), null);
	strictEqual(parse('true'), true);
	strictEqual(parse('false'), false);
	strictEqual(parse('"null"'), "null");
	strictEqual(parse('5'), 5);
	strictEqual(parse('-5'), -5);
	strictEqual(parse('3.14'), 3.14);
	strictEqual(parse('-3.14'), -3.14);
	deepEqual(parse('{}'), {});
	deepEqual(parse('[]'), []);
})