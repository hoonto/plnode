# Copyright (c) 2013 Hoonto/Matt
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
    'variables': {
        'PLV8_VERSION': '1.5.0-dev1',
        'INCLUDEDIR-SERVER': '/usr/include/pgsql/server',
    },
    'defines': [
      '_GNU_SOURCE',
      'LANG_plnode',
    ],
    'targets': [
    {
        'target_name': 'plnode',
        'type': '<(library)',
        'sources': [
            'plnode_config.h',
            'plnode.h',
            'plnode_param.h',
            'coffee-script.cc',
            'livescript.cc',
            'plnode.cc',
            'plnode_func.cc',
            'plnode_param.cc',
            'plnode_type.cc',
        ],
        'include_dirs': [
            '.',
            '/usr/include/pgsql/server',
            '../v8/include',
            '../uv/include',
            '/usr/include/pgsql/internal',
            '/usr/include/libxml2',
            '/usr/include/libxml2',
            '../../src',
        ],
        'cflags!': [ '-fno-exceptions' ],
        'cflags_cc!': [ '-fno-exceptions' ],
        'conditions': [
            ['OS=="mac"', {
                'xcode_settings': {
                    'GCC_ENABLE_CPP_EXCEPTIONS': 'YES'
                }
            }]
        ],

        #'actions': [
        #{
        #    'action_name': 'plnode_config.h',
        #    'inputs': [
        #        'plnode_config.h.in'
        #    ],
        #    'outputs': [
        #        'plnode_config.h',
        #    ],
        #    'action': ['sed', '-e', 's/^\#undef PLV8_VERSION/#define PLV8_VERSION "$(PLV8_VERSION)"/', '$< > $@'],
        #},
        #],

    }],
}
