{
	'targets': [
		{
			'target_name': 'ijson_native',
			'include_dirs': [
				'<@(nodedir)/deps/v8/src',
			],
			'sources': [
				'src/ijson.cc',
			],
			'cflags!': ['-ansi' '-O3'],
		},
	],
}
