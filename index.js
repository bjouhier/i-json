/**
 * Copyright (c) 2014 Bruno Jouhier <bjouhier@gmail.com>
 * MIT License
 */
"use strict";

var classes = [];
var lastClass = 0;

function makeClass(str) {
	for (var i = 0; i < str.length; i++) {
		var ch = str.charCodeAt(i);
		if (classes[ch] != null) throw new Error("duplicate class: " + str[i]);
		classes[ch] = lastClass;
	}
	return lastClass++;
}

// basic classes
var CURLY_OPEN = makeClass('{'),
	CURLY_CLOSE = makeClass('}'),
	SQUARE_OPEN = makeClass('['),
	SQUARE_CLOSE = makeClass(']'),
	COMMA = makeClass(','),
	COLON = makeClass(':'),
	DQUOTE = makeClass('"'),
	BSLASH = makeClass('\\'),
	SPACE = makeClass(' \t\r'),
	NL = makeClass('\n'),
	t_ = makeClass('t'),
	r_ = makeClass('r'),
	u_ = makeClass('u'),
	e_ = makeClass('e'),
	f_ = makeClass('f'),
	a_ = makeClass('a'),
	l_ = makeClass('l'),
	s_ = makeClass('s'),
	n_ = makeClass('n'),
	PLUS = makeClass('+'),
	MINUS = makeClass('-'),
	DIGIT = makeClass('0123456789'),
	DOT = makeClass('.'),
	E_ = makeClass('E'),
	HEX_REMAIN = makeClass('ABCDFbcd');

for (var i = 0; i < 0x80; i++) {
	if (classes[i] == null) classes[i] = lastClass;
}

function makeState(transitions, def) {
	var tmp = [];
	for (var i = 0; i < transitions.length; i++) {
		var transition = transitions[i];
		tmp[transition[0]] = transition[1];
	}
	var state = [];
	for (var i = 0; i <= lastClass; i++) {
		state[i] = tmp[i] === undefined ? def : tmp[i];
	}
	return state;
}

function literalOpen(val) {
	var str = '' + val;

	function insideLit(i, nextFn) {
		var ch = str.charCodeAt(i);
		var state = [];
		for (var j = 0; j <= lastClass; j++) {
			state[j] = j === classes[ch] ? nextFn : error;
		}
		return function(parser, pos) {
			return state;
		}
	}
	var fn = function(parser, pos, cla) {
			parser.frame.setValue(val);
			return AFTER_VALUE;
		};
	for (var i = str.length - 1; i >= 1; i--) {
		fn = insideLit(i, fn);
	}
	return fn;
}

function escapeOpen(parser, pos) {
	parser.keep += parser.source.substring(parser.beg, pos);
	parser.beg = -1;
	return AFTER_ESCAPE;
}

function escapeLetter(ch) {
	return function(parser, pos) {
		parser.keep += ch;
		parser.beg = pos + 1;
		return INSIDE_QUOTES;
	}
}

function escapeUnicode(parser, pos) {
	throw new Error("NIY");
}

var BEFORE_VALUE = makeState([
	[CURLY_OPEN, objectOpen],
	[SQUARE_OPEN, arrayOpen],
	[DQUOTE, stringOpen],
	[MINUS, numberOpen],
	[DIGIT, numberOpen],
	[t_, literalOpen(true)],
	[f_, literalOpen(false)],
	[n_, literalOpen(null)],
	[SPACE, null],
	[NL, eatNL],
	[SQUARE_CLOSE, arrayClose]
], error);

var AFTER_VALUE = makeState([
	[COMMA, eatComma],
	[CURLY_CLOSE, objectClose],
	[SQUARE_CLOSE, arrayClose],
	[SPACE, null],
	[NL, eatNL]
], error);

// object states
var BEFORE_KEY = makeState([
	[DQUOTE, stringOpen],
	[CURLY_CLOSE, objectClose],
	[SPACE, null],
	[NL, eatNL]
], error);

var AFTER_KEY = makeState([
	[COLON, eatColon],
	[SPACE, null],
	[NL, eatNL]
], error);

// string states
var INSIDE_QUOTES = makeState([
	[DQUOTE, stringClose],
	[BSLASH, escapeOpen],
	[NL, error]
], null);

var AFTER_ESCAPE = makeState([
	[n_, escapeLetter('\n')],
	[r_, escapeLetter('\r')],
	[t_, escapeLetter('\t')],
	[DQUOTE, escapeLetter('\"')],
	[u_, escapeUnicode]
], error);

// number transition - a bit loose, let parse handle errors
var INSIDE_NUMBER = makeState([
	[DIGIT, null],
	[DOT, doubleOpen],
	[e_, expOpen],
	[E_, expOpen]
], numberClose);

var INSIDE_DOUBLE = makeState([
	[DIGIT, null],
	[e_, expOpen],
	[E_, expOpen]
], numberClose);

var INSIDE_EXP = makeState([
	[PLUS, null],
	[MINUS, null],
	[DIGIT, null]
], numberClose);

function Frame(result, key, prev, isArray) {
	this.result = result;
	this.key = key;
	this.prev = prev;
	this.isArray = isArray;
}

Frame.prototype.setValue = function(val) {
	//console.log("setValue: key=" + this.key + ", value=" + val);
	if (this.isArray) {
		this.result.push(val);
	} else {
		this.result[this.key] = val;
		this.key = null;
	}
}

function numberOpen(parser, pos) {
	parser.isDouble = false;
	parser.beg = pos;
	return INSIDE_NUMBER;
}

function doubleOpen(parser, pos) {
	parser.isDouble = true;
	return INSIDE_DOUBLE;
}

function expOpen(parser, pos) {
	parser.isDouble = true;
	return INSIDE_EXP;
}

function numberClose(parser, pos, cla) {
	var str = parser.source.substring(parser.beg, pos);
	parser.beg = -1;
	if (parser.keep.length !== 0) {
		str = parser.keep + str;
		parser.keep = "";		
	}
	parser.frame.setValue(parser.isDouble ? parseFloat(str) : parseInt(str, 10));
	var fn = AFTER_VALUE[cla];
	return fn ? fn(parser, pos, cla) : AFTER_VALUE;
}

function stringOpen(parser, pos) {
	parser.beg = pos + 1;
	return INSIDE_QUOTES;
}

function stringClose(parser, pos) {
	var val = parser.source.substring(parser.beg, pos);
	parser.beg = -1;
	if (parser.keep.length !== 0) {
		val = parser.keep + val;
		parser.keep = "";
	}
	var frame = parser.frame;
	if (frame.key === null && !frame.isArray) {
		frame.key = val;
		return AFTER_KEY;
	} else {
		frame.setValue(val);
		return AFTER_VALUE;
	}
}

function arrayOpen(parser, pos) {
	parser.frame = new Frame([], null, parser.frame, true);
	return BEFORE_VALUE;
}

function arrayClose(parser, pos) {
	var val = parser.frame.result;
	parser.frame = parser.frame.prev;
	parser.frame.setValue(val);
	return AFTER_VALUE;
}

function objectOpen(parser, pos) {
	parser.frame = new Frame({}, null, parser.frame, false);
	return BEFORE_KEY;
}

function objectClose(parser, pos) {
	var val = parser.frame.result;
	parser.frame = parser.frame.prev;
	parser.frame.setValue(val);
	return AFTER_VALUE;
}

function eatColon(parser, pos) {
	return BEFORE_VALUE;
}

function eatComma(parser, pos) {
	var frame = parser.frame;
	frame.key = null;
	return frame.isArray ? BEFORE_VALUE : BEFORE_KEY;
}

function eatNL(parser, pos, cla, state) {
	parser.line++;
	return state;
}

function error(parser, pos) {
	throw new Error("JSON syntax error line " + parser.line + ", near " + parser.source.substring(pos, pos + 10));
}

function Parser() {
	this.frame = new Frame([], null, null, true);
	this.line = 1;
	this.keep = "";
	this.isDouble = false;
	this.beg = -1;
	this.state = BEFORE_VALUE;
}

function parse(parser, str, state) {
	var pos = 0,
		len = str.length;
	while (pos < len) {
		var ch = str.charCodeAt(pos);
		var cla = (ch < 0x80) ? classes[ch] : lastClass;
		var fn = state[cla];
		//if (fn === error) console.log("ch=" + str[pos] + ", cla=" + cla, state)
		if (fn !== null) state = fn(parser, pos, cla, state);
		pos++;
	}
	return state;	
}

Parser.prototype.update = function(str) {
	this.source = str;
	this.state = parse(this, str, this.state);
	if (this.beg !== -1) {
		this.keep += this.source.substring(this.beg);
		this.beg = 0;
	}
}

Parser.prototype.result = function() {
	if (this.frame.prev) throw new Error("Unexpected end of input text");
	if (this.frame.result.length > 1) throw new Error("Too many results: " + this.frame.result.length);
	return this.frame.result[0];
}

exports.createParser = function() {
	return new Parser();
}