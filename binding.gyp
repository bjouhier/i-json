{
	'targets': [
		{
			'target_name': 'ijson_bindings',
			'include_dirs': [
				'<@(nodedir)/deps/v8/src',
			],
			'sources': [
				'src/parser.cc',
			],
			'cflags!': ['-ansi' '-O3'],
		},
	],
}
