# Fast incremental JSON parser

i-json is a fast incremental JSON parser implemented in C++. 

See https://github.com/joyent/node/issues/7543#issuecomment-43974821 for background discussion.

The focus of i-json is on performance. The API is minimal and designed for speed but i-json can be used as a fast engine to power higher level JSON APIs (evented, streaming). 

## Installation

``` sh
npm install i-json
```

## API

```javascript
var ijson = require('i-json');

var parser = ijson.createParser();

// update the parser with the next piece of JSON buffer.
// This call will typically be issued from a 'data' event handler
parser.update(jsonChunk);

// retrieve the result
var obj = parser.result();
```

You can pass a `Buffer` or a string to `parser.update`. If you get your input data in a `Buffer`, you should pass it directly to `parser.update`; you should not convert it and pass it as a string. 

You can also configure a callback which will be called during parsing:

```
var parser = ijson.createParser(callback, maxDepth);

function callback(value, path) {
	// ...
	return value;
}
```

`callback` receives the value and the path to the value. `path` is an array of object keys and array indexes that lead to the value.  

The callback also allows you transform the value. If you return `undefined` from the callback, the parsed value will not be recorded into the final result. So you can implement a high level evented API by emitting events from the callback and returning `undefined`.

`maxDepth` lets you control the granularity of the callbacks. The callback will only be called when the depth of parsing is <= `maxDepth`. If you omit `maxDepth` the callback will be called on all the values.

## Example

``` javascript
var ijson = require('ijson');

var parser = ijson.createParser(function(value, path) {
	console.log(path.length + ": " + path.join('/') + ": " + JSON.stringify(value));
	return value;
}, 2);

parser.update(new Buffer('{"data": [2, 3, [true, false]], "message": "hello" }'));
console.log("result=" + JSON.stringify(parser.result()));
```
Output:
```
2: data/0: 2
2: data/1: 3
2: data/2: [true,false]
1: data: [2,3,[true,false]]
1: message: "hello"
0: : {"data":[2,3,[true,false]],"message":"hello"}
result={"data":[2,3,[true,false]],"message":"hello"}
```

Note that the `true` and `false` values are not output individually, but they will if we increase or omit `maxDepth`.

## Performance

Typical results of the test program (parsing an 8 MB file 10 times) on my MBP i7

```
JSON.parse: 632 ms
I-JSON single chunk: 1046 ms
I-JSON multiple chunks: 1278 ms
```

## License

[MIT license](http://en.wikipedia.org/wiki/MIT_License).
