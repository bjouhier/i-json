"use strict";

try {
	var nat = require('./build/Release/ijson_bindings');
	exports.createParser = function(cb, depth) {
		var p = new nat.Parser(cb, depth);
		return p;
	};
} catch (ex) {
	console.log("cannot load C++ parser, using JS implementation");
	exports.createParser = require('./lib/parser').createParser;
}