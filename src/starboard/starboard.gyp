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

# The common "starboard" target. Any target that depends on Starboard should
# depend on this common target, and not any of the specific "starboard_platform"
# targets.

{
  'targets': [
    {
      'target_name': 'starboard',
      'type': 'none',
      'sources': [
        'atomic.h',
        'condition_variable.h',
        'configuration.h',
        'directory.h',
        'double.h',
        'export.h',
        'file.h',
        'log.h',
        'memory.h',
        'mutex.h',
        'string.h',
        'system.h',
        'thread.h',
        'thread_types.h',
        'time.h',
        'time_zone.h',
        'types.h',
      ],
      'conditions': [
        ['starboard_path == ""', {
          # TODO: Make starboard_path required. This legacy condition is only
          # here to support starboard-linux while it still exists.
          'dependencies': [
            '<(DEPTH)/starboard/<(target_arch)/starboard_platform.gyp:starboard_platform',
          ],
          'export_dependent_settings': [
            '<(DEPTH)/starboard/<(target_arch)/starboard_platform.gyp:starboard_platform',
          ],
        }, {
          'dependencies': [
            '<(DEPTH)/<(starboard_path)/starboard_platform.gyp:starboard_platform',
          ],
          'export_dependent_settings': [
            '<(DEPTH)/<(starboard_path)/starboard_platform.gyp:starboard_platform',
          ],
        }],
      ],
    },
  ],
}
