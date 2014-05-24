# Fast incremental JSON parser

i-json is a fast incremental JSON parser implemented in pure JS. 

See https://github.com/joyent/node/issues/7543#issuecomment-43974821 for background discussion

## API

```javascript
var ijson = require('i-json');

var parser = ijson.createParser();

// update the parser with the next piece of JSON text.
// This call will typically be issued from a 'data' event handler
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
JSON: 622 ms
I-JSON single chunk: 1661 ms
I-JSON multiple chunks: 1800 ms
```

It is a bit faster than json2, which is implemented with regexp sanitization and `eval`.

# License

[MIT license](http://en.wikipedia.org/wiki/MIT_License).
