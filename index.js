"use strict";

try {
	var nat = require('./build/Release/ijson_bindings');
	exports.createParser = function() {
		var p = new nat.Parser();
		return p;
	};
} catch (ex) {
	console.log("cannot load C++ parser, using JS implementation");
	exports.createParser = require('./lib/parser').createParser;
}