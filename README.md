# Fast incremental JSON parser

i-json is a fast incremental JSON parser implemented in C++. 

See https://github.com/joyent/node/issues/7543#issuecomment-43974821 for background discussion

## API

```javascript
var ijson = require('i-json');

var parser = ijson.createParser();

// update the parser with the next piece of JSON buffer.
// This call will typically be issued from a 'data' event handler
// Note: jsonChunk must be a buffer, not a string
parser.update(jsonChunk);

// retrieve the result
var obj = parser.result();
```

## Installation

``` sh
npm install i-json
```

# Performance

Typical results of the test program on my MBP i7

```
JSON.parse: 632 ms
I-JSON single chunk: 1046 ms
I-JSON multiple chunks: 1278 ms
```

# TODO

* Implement \uxxxx unicode sequences
* Cleanup packaging with fallback to JS implementation if binary module is not available.

# License

[MIT license](http://en.wikipedia.org/wiki/MIT_License).
