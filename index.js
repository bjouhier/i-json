"use strict";

try {
	var nat = require('./build/Release/ijson_bindings');
	nat.Parser.prototype.update = function(arg) {
		if (typeof arg == "string") return this._update(new Buffer(arg, 'utf8'));
		else return this._update(arg);
	}
	exports.createParser = function(cb, depth) {
		var p = new nat.Parser(cb, depth);
		return p;
	};
} catch (ex) {
	console.log("cannot load C++ parser, using JS implementation");
	exports.createParser = require('./lib/parser').createParser;
}