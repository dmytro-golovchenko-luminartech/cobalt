# Copyright 2015 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

{
  'variables': {
    'cobalt_code': 1,
  },
  'targets': [
    {
      'target_name': 'browser',
      'type': 'static_library',
      'sources': [
        'application.cc',
        'application.h',
        'browser_module.cc',
        'browser_module.h',
        'debug_console.cc',
        'debug_console.h',
        'h5vcc_url_handler.cc',
        'h5vcc_url_handler.h',
        'memory_settings/memory_settings.cc',
        'memory_settings/memory_settings.h',
        'memory_tracker/buffered_file_writer.cc',
        'memory_tracker/buffered_file_writer.h',
        'memory_tracker/memory_tracker_tool.cc',
        'memory_tracker/memory_tracker_tool.h',
        'memory_tracker/memory_tracker_tool_impl.cc',
        'memory_tracker/memory_tracker_tool_impl.h',
        'render_tree_combiner.cc',
        'render_tree_combiner.h',
        'resource_provider_array_buffer_allocator.cc',
        'resource_provider_array_buffer_allocator.h',
        'splash_screen.cc',
        'splash_screen.h',
        'storage_upgrade_handler.cc',
        'storage_upgrade_handler.h',
        'switches.cc',
        'switches.h',
        'trace_manager.cc',
        'trace_manager.h',
        'url_handler.cc',
        'url_handler.h',
        'web_module.cc',
        'web_module.h',
        'web_module_stat_tracker.cc',
        'web_module_stat_tracker.h',
      ],
      'defines': [
        'COBALT_SKIA_CACHE_SIZE_IN_BYTES=<(skia_cache_size_in_bytes)',
        'COBALT_SKIA_GLYPH_ATLAS_WIDTH=<(skia_glyph_atlas_width)',
        'COBALT_SKIA_GLYPH_ATLAS_HEIGHT=<(skia_glyph_atlas_height)',
        'COBALT_IMAGE_CACHE_SIZE_IN_BYTES=<(image_cache_size_in_bytes)',
        'COBALT_REMOTE_TYPEFACE_CACHE_SIZE_IN_BYTES=<(remote_font_cache_size_in_bytes)',
        'COBALT_IMAGE_CACHE_CAPACITY_MULTIPLIER_WHEN_PLAYING_VIDEO=<(image_cache_capacity_multiplier_when_playing_video)',
        'COBALT_SOFTWARE_SURFACE_CACHE_SIZE_IN_BYTES=<(software_surface_cache_size_in_bytes)',
        'COBALT_JS_GARBAGE_COLLECTION_THRESHOLD_IN_BYTES=<(mozjs_garbage_collection_threshold_in_bytes)',
      ],
      'dependencies': [
        '<@(cobalt_platform_dependencies)',
        '<(DEPTH)/cobalt/accessibility/accessibility.gyp:accessibility',
        '<(DEPTH)/cobalt/account/account.gyp:account',
        '<(DEPTH)/cobalt/audio/audio.gyp:audio',
        '<(DEPTH)/cobalt/base/base.gyp:base',
        '<(DEPTH)/cobalt/css_parser/css_parser.gyp:css_parser',
        '<(DEPTH)/cobalt/debug/debug.gyp:debug',
        '<(DEPTH)/cobalt/dom/dom.gyp:dom',
        '<(DEPTH)/cobalt/dom_parser/dom_parser.gyp:dom_parser',
        '<(DEPTH)/cobalt/h5vcc/h5vcc.gyp:h5vcc',
        '<(DEPTH)/cobalt/input/input.gyp:input',
        '<(DEPTH)/cobalt/layout/layout.gyp:layout',
        '<(DEPTH)/cobalt/math/math.gyp:math',
        '<(DEPTH)/cobalt/media_session/media_session.gyp:media_session',
        '<(DEPTH)/cobalt/network/network.gyp:network',
        '<(DEPTH)/cobalt/renderer/renderer.gyp:renderer',
        '<(DEPTH)/cobalt/script/engine.gyp:engine',
        '<(DEPTH)/cobalt/speech/speech.gyp:speech',
        '<(DEPTH)/cobalt/system_window/system_window.gyp:system_window',
        '<(DEPTH)/cobalt/trace_event/trace_event.gyp:trace_event',
        '<(DEPTH)/cobalt/webdriver/webdriver.gyp:webdriver',
        '<(DEPTH)/cobalt/websocket/websocket.gyp:websocket',
        '<(DEPTH)/cobalt/xhr/xhr.gyp:xhr',
        '<(DEPTH)/googleurl/googleurl.gyp:googleurl',
        '<(DEPTH)/nb/nb.gyp:nb',
        'browser_bindings.gyp:bindings',
        'screen_shot_writer',
      ],
      # This target doesn't generate any headers, but it exposes generated
      # header files (for idl dictionaries) through this module's public header
      # files. So mark this target as a hard dependency to ensure that any
      # dependent targets wait until this target (and its hard dependencies) are
      # built.
      #'hard_dependency': 1,
      'export_dependent_settings': [
        # Additionally, ensure that the include directories for generated
        # headers are put on the include directories for targets that depend
        # on this one.
        '<(DEPTH)/cobalt/dom/dom.gyp:dom',
      ],
      'conditions': [
        ['enable_about_scheme == 1', {
          'defines': [ 'ENABLE_ABOUT_SCHEME' ],
        }],
        ['enable_map_to_mesh == 1', {
          'defines' : ['ENABLE_MAP_TO_MESH'],
        }],
        ['cobalt_media_source_2016==1', {
          'dependencies': [
            '<(DEPTH)/cobalt/media/media2.gyp:media2',
          ],
        }, {
          'dependencies': [
            '<(DEPTH)/cobalt/media/media.gyp:media',
          ],
        }],
        ['mesh_cache_size_in_bytes == "auto"', {
          'conditions': [
            ['enable_map_to_mesh==1', {
              'defines': [
                'COBALT_MESH_CACHE_SIZE_IN_BYTES=1*1024*1024',
              ],
            }, {
              'defines': [
                'COBALT_MESH_CACHE_SIZE_IN_BYTES=0',
              ],
            }],
          ],
        }, {
          'defines': [
            'COBALT_MESH_CACHE_SIZE_IN_BYTES=<(mesh_cache_size_in_bytes)',
          ],
        }],
      ],
    },

    {
      # This target provides functionality for creating screenshots of a
      # render tree and writing it to disk.
      'target_name': 'screen_shot_writer',
      'type': 'static_library',
      'variables': {
        # This target includes non-Cobalt code that produces pendantic
        # warnings as errors.
        'cobalt_code': 0,
      },
      'conditions': [
        ['enable_screenshot==1', {
          'sources': [
            'screen_shot_writer.h',
            'screen_shot_writer.cc',
          ],
          'dependencies': [
            '<(DEPTH)/cobalt/base/base.gyp:base',
            '<(DEPTH)/cobalt/renderer/rasterizer/skia/skia/skia.gyp:skia',
            '<(DEPTH)/cobalt/renderer/test/png_utils/png_utils.gyp:png_utils',
          ],
          'all_dependent_settings': {
            'defines': [ 'ENABLE_SCREENSHOT', ],
          },
        }],
      ],
    },

    {
      'target_name': 'browser_test',
      'type': '<(gtest_target_type)',
      'sources': [
        'storage_upgrade_handler_test.cc',
        'memory_settings/memory_settings_test.cc',
        'memory_tracker/memory_tracker_tool_test.cc',
      ],
      'defines': [
        'COBALT_SKIA_CACHE_SIZE_IN_BYTES=<(skia_cache_size_in_bytes)',
        'COBALT_SKIA_GLYPH_ATLAS_WIDTH=<(skia_glyph_atlas_width)',
        'COBALT_SKIA_GLYPH_ATLAS_HEIGHT=<(skia_glyph_atlas_height)',
        'COBALT_IMAGE_CACHE_SIZE_IN_BYTES=<(image_cache_size_in_bytes)',
        'COBALT_SOFTWARE_SURFACE_CACHE_SIZE_IN_BYTES=<(software_surface_cache_size_in_bytes)',
        'COBALT_JS_GARBAGE_COLLECTION_THRESHOLD_IN_BYTES=<(mozjs_garbage_collection_threshold_in_bytes)',
      ],
      'dependencies': [
        '<(DEPTH)/cobalt/base/base.gyp:base',
        '<(DEPTH)/cobalt/dom/dom.gyp:dom',
        '<(DEPTH)/cobalt/network/network.gyp:network',
        '<(DEPTH)/cobalt/speech/speech.gyp:speech',
        '<(DEPTH)/cobalt/storage/storage.gyp:storage',
        '<(DEPTH)/cobalt/storage/storage.gyp:storage_upgrade_copy_test_data',
        '<(DEPTH)/cobalt/test/test.gyp:run_all_unittests',
        '<(DEPTH)/testing/gmock.gyp:gmock',
        '<(DEPTH)/testing/gtest.gyp:gtest',
        'browser',
      ],
    },

    {
      'target_name': 'browser_test_deploy',
      'type': 'none',
      'dependencies': [
        'browser_test',
      ],
      'variables': {
        'executable_name': 'browser_test',
      },
      'includes': [ '../../starboard/build/deploy.gypi' ],
    },

    {
      'target_name': 'browser_copy_test_data',
      'type': 'none',
      'actions': [
        {
          'action_name': 'browser_copy_test_data',
          'variables': {
            'input_files': [
              '<(DEPTH)/cobalt/browser/testdata/',
            ],
            'output_dir': 'cobalt/browser/testdata/',
          },
          'includes': [ '../build/copy_test_data.gypi' ],
        },
      ],
    },

    {
      'target_name': 'browser_copy_debug_console',
      'type': 'none',
      'actions': [
        {
          'action_name': 'browser_copy_debug_console',
          'variables': {
            'input_files': [
              '<(DEPTH)/cobalt/browser/debug_console/',
            ],
            'output_dir': 'cobalt/browser/debug_console/',
          },
          'includes': [ '../build/copy_web_data.gypi' ],
        },
      ],
    },
  ],
}
