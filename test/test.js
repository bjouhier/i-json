var ijson = require('..');
const { strictEqual, deepEqual } = require('chai').assert;

describe('i-json parser', function() {
    function testPass(prefix, parseOk) {
        function parseError(str) {
            try {
                return parseOk(str);
            } catch (ex) {
                return ex.message;
            }
        }

        function testStrict(str) {
            strictEqual(parseOk(str), JSON.parse(str));
        }

        function testDeep(str) {
            deepEqual(parseOk(str), JSON.parse(str));
        }

        it(prefix + 'basic values', function() {
            testStrict('null');
            testStrict('true');
            testStrict('false');
            testStrict('"null"');
            testStrict('""');
            testStrict('"‘"');
            testStrict('5');
            testStrict('-5');
            testStrict('3.14');
            testStrict('-3.14');
            testStrict('6.02e23');
            testStrict('-6.02e+23');
            testStrict('6.62E-34');
            testStrict('9007199254740992');
            testStrict('-9007199254740992');
            testDeep('{}');
            testDeep('[]');
        });

        it(prefix + 'complex values', function() {
            testDeep('{"a":{"b":{"c":1,"d":2},"e":[3,4],"f":[true,false,null]}}');
        });

        it(prefix + 'extra spaces', function() {
            testStrict(' null ');
            testStrict(' 5 ');
            testStrict(' "" ');
            testDeep(' { "a": {"b" :{"c":1,"d":2 },"e" : [ 3 ,4 ],"f":[ true , false, null]}}');
            testDeep('{\n"value": 3\n}'); // issue #9
        });

        it(prefix + 'escape sequences', function() {
            testStrict('"\\b"');
            testStrict('"\\f"');
            testStrict('"\\n"');
            testStrict('"\\r"');
            testStrict('"\\t"');
            testStrict('"\\""');
            testStrict('"\\\\"');
            testStrict('"\\u0040"');
            testStrict('"\\u00e9"');
            testStrict('"\\u20AC"');
            testStrict('"a\\r\\nbc\\td\\"\\\\e\\u20ACf"');
        });

        it(prefix + 'errors', function() {
            function trim(s) {
                return /^incremental/.test(prefix) ? s.substring(0, s.length - 1) : s;
            }
            strictEqual(parseError(''), 'Unexpected end of input');
            strictEqual(parseError('  '), 'Unexpected end of input');
            strictEqual(parseError('a'), 'line 1: syntax error near a');
            strictEqual(parseError('\na'), 'line 2: syntax error near a');
            strictEqual(parseError('""a'), 'line 1: syntax error near a');
            strictEqual(parseError('"a'), 'Unexpected end of input');
            strictEqual(parseError('"a\nb"'), 'line 1: syntax error near ');
            strictEqual(parseError('}'), 'line 1: syntax error near }');
            strictEqual(parseError(']'), 'line 1: syntax error near ]');
            strictEqual(parseError('{a}'), trim('line 1: syntax error near a}'));
            strictEqual(parseError('{"a}'), 'Unexpected end of input');
            strictEqual(parseError('{"a",}'), trim('line 1: syntax error near ,}'));
            strictEqual(parseError('{"a"b}'), trim('line 1: syntax error near b}'));
            strictEqual(parseError('{"a":b}'), trim('line 1: syntax error near b}'));
            strictEqual(parseError('{"a":"b",}'), 'line 1: syntax error near }');
            strictEqual(parseError('{"a":"b"]'), 'line 1: syntax error near ]');
            strictEqual(parseError('[a]'), trim('line 1: syntax error near a]'));
            strictEqual(parseError('["a"'), 'Unexpected end of input');
            strictEqual(parseError('["a",'), 'Unexpected end of input');
            strictEqual(parseError('["a",]'), 'line 1: syntax error near ]');
            strictEqual(parseError('["a"}'), 'line 1: syntax error near }');
        });
    }

    testPass('buffer: ', function parseOk(data) {
        var parser = ijson.createParser();
        parser.update(new Buffer(data, 'utf8'));
        return parser.result();
    });

    testPass('full: ', function parseOk(data) {
        var parser = ijson.createParser();
        parser.update(data);
        return parser.result();
    });

    testPass('incremental: ', function parseOk(data) {
        var parser = ijson.createParser();
        for (var i = 0; i < data.length; i++) parser.update(data[i]);
        return parser.result();
    });

    testPass('full callback: ', function parseOk(data) {
        var r;
        var parser = ijson.createParser(function(result) {
            r = result;
        }, 0);
        parser.update(data);
        parser.result(); // for numbers
        return r;
    });

    testPass('incremental callback: ', function parseOk(data) {
        var r;
        var parser = ijson.createParser(function(result) {
            r = result;
        }, 0);
        for (var i = 0; i < data.length; i++) parser.update(data[i]);
        parser.result(); // for numbers
        return r;
    });

    it('callback depth 0', function() {
        var results = [];
        var parser = ijson.createParser(function(result, path) {
            results.push(path.join('/') + ': ' + JSON.stringify(result));
            return result;
        }, 0);
        parser.update('{"data": [2, 3, [true, false]], "message": "hello" }');
        strictEqual(results.length, 1);
        strictEqual(results[0], ': {"data":[2,3,[true,false]],"message":"hello"}');
        strictEqual(JSON.stringify(parser.result()), '{"data":[2,3,[true,false]],"message":"hello"}');
    });

    it('callback depth 2', function() {
        var results = [];
        var parser = ijson.createParser(function(result, path) {
            results.push(path.join('/') + ': ' + JSON.stringify(result));
            return result;
        }, 2);
        parser.update('{"data": [2, 3, [true, false]], "message": "hello" }');
        strictEqual(results.length, 6);
        strictEqual(results[0], 'data/0: 2');
        strictEqual(results[1], 'data/1: 3');
        strictEqual(results[2], 'data/2: [true,false]');
        strictEqual(results[3], 'data: [2,3,[true,false]]');
        strictEqual(results[4], 'message: "hello"');
        strictEqual(results[5], ': {"data":[2,3,[true,false]],"message":"hello"}');
        strictEqual(JSON.stringify(parser.result()), '{"data":[2,3,[true,false]],"message":"hello"}');
    });

    it('callback depth unlimited', function() {
        var results = [];
        var parser = ijson.createParser(function(result, path) {
            results.push(path.join('/') + ': ' + JSON.stringify(result));
            return result;
        });
        parser.update('{"data": [2, 3, [true, false]], "message": "hello" }');
        strictEqual(results.length, 8);
        strictEqual(results[0], 'data/0: 2');
        strictEqual(results[1], 'data/1: 3');
        strictEqual(results[2], 'data/2/0: true');
        strictEqual(results[3], 'data/2/1: false');
        strictEqual(results[4], 'data/2: [true,false]');
        strictEqual(results[5], 'data: [2,3,[true,false]]');
        strictEqual(results[6], 'message: "hello"');
        strictEqual(results[7], ': {"data":[2,3,[true,false]],"message":"hello"}');
        strictEqual(JSON.stringify(parser.result()), '{"data":[2,3,[true,false]],"message":"hello"}');
    });

    it('callback no return', function() {
        var results = [];
        var parser = ijson.createParser(function(result, path) {
            results.push(path.join('/') + ': ' + JSON.stringify(result));
        });
        parser.update('{"data": [2, 3, [true, false]], "message": "hello" }');
        strictEqual(results.length, 8);
        strictEqual(results[0], 'data/0: 2');
        strictEqual(results[1], 'data/1: 3');
        strictEqual(results[2], 'data/2/0: true');
        strictEqual(results[3], 'data/2/1: false');
        strictEqual(results[4], 'data/2: []');
        strictEqual(results[5], 'data: []');
        strictEqual(results[6], 'message: "hello"');
        strictEqual(results[7], ': {}');
        strictEqual(JSON.stringify(parser.result()), undefined);
    });

    it('escaped forward slash', function() {
        var results = [];
        var parser = ijson.createParser(function(result, path) {
            results.push(path.join('/') + ': ' + JSON.stringify(result));
        });
        parser.update('{"data": [2, 3, [true, false]], "message": "hello/goodbye" }');
        strictEqual(results.length, 8);
        strictEqual(results[0], 'data/0: 2');
        strictEqual(results[1], 'data/1: 3');
        strictEqual(results[2], 'data/2/0: true');
        strictEqual(results[3], 'data/2/1: false');
        strictEqual(results[4], 'data/2: []');
        strictEqual(results[5], 'data: []');
        strictEqual(results[6], 'message: "hello/goodbye"');
        strictEqual(results[7], ': {}');
        strictEqual(JSON.stringify(parser.result()), undefined);
    });
});
