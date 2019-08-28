"use strict";
var fs = require('fs');
var ijson = require('../index');

var big = fs.readFileSync(__dirname + '/big.json');
var ITER = 10;

function test(msg, fn, data) {
	var result;
	var t0 = Date.now();
	for (var i = 0; i < ITER; i++) result = fn(data);
	console.log(msg + ': ' + (Date.now() - t0) + " ms");
	return result;
}

function check(r1, r2) {
	var s1 = JSON.stringify(r1, null, '\t');
	var s2 = JSON.stringify(r2, null, '\t');
	if (s1 === s2) {
		//console.log("SAME RESULTS!");
	} else {
		console.log("DIFFERENT RESULTS!");
		var a1 = s1.split('\n');
		var a2 = (s2 || '').split('\n');
		for (var i = 0; i < a1.length; i++) {
			if (a1[i] !== a2[i]) {
				console.log((i + 1) + ": a1=" + a1[i]);
				console.log((i + 1) + ": a2=" + a2[i]);
				break;
			}
		}
	}
}

for (var pass = 1; pass <= 100; pass++) {
	console.log("*** PASS " + pass + " ***");
	var r1 = test("JSON.parse", JSON.parse, big);

	var r2;
	test("I-JSON single chunk", function(data) {
		var parser = ijson.createParser(function(val, path) {
			r2 = val;
		}, 0);
		parser.update(data);
		return parser.result();
	}, big);

	check(r1, r2);

	var r3;
	test("I-JSON multiple chunks", function(data) {
		var parser = ijson.createParser(function(val, path) {
			r3 = val;
		}, 0);
		var pos = 0;
		var len = data.length;
		while (pos < len) {
			var l = Math.floor(Math.random() * 8192) + 1;
			if (pos + l > len) l = len - pos;
			parser.update(data.slice(pos, pos + l));
			pos += l;
		}
		return parser.result();
	}, big);

	check(r1, r3);

	try {
		var jsonparse = require('jsonparse');
		var r4 = test("jsonparse single chunk", function(data) {
			var parser = new jsonparse();
			var result;
			parser.onValue = function(value) {
				if (this.stack.length === 0) result = value;
			}
			parser.write(data);
			return result;
		}, big.toString('utf8'));
		check(r1, r4);
	} catch (ex) {
		//console.log("skipping jsonparse test: " + ex.message);
	}

	try {
		var clarinet = require('clarinet');
		var r5 = test("clarinet single chunk", function(data) {
			var parser = new clarinet.parser();
			parser.write(data).close();
		}, big.toString('utf8'));
		console.log("clarinet does not materialize result, time is for parsing only");
		//check(r1, r5);
	} catch (ex) {
		//console.log("skipping clarinet test: " + ex.message);
	}
	console.log(process.memoryUsage());
}