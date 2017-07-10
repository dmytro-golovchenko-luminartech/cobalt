#!/usr/bin/python2
"""Simple benchmark for starting a video from browse."""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import os
import sys

# Add the base webdriver_benchmarks path
sys.path.insert(0,
                os.path.dirname(
                    os.path.dirname((os.path.dirname(
                        os.path.dirname(os.path.realpath(__file__)))))))

# pylint: disable=C6204,C6203
import tv_testcase
import tv_testcase_event_recorder
import tv_testcase_util

# selenium imports
keys = tv_testcase_util.import_selenium_module("webdriver.common.keys")

MAX_VIDEO_FAILURE_COUNT = 10
MAX_SKIPPABLE_AD_COUNT = 8

NUM_LOAD_TV_CALLS = 4
NUM_ITERATIONS_PER_LOAD_TV_CALL = 25

BROWSE_TO_WATCH_EVENT_NAME = "wbBrowseToWatch"
BROWSE_TO_WATCH_EVENT_TYPE = tv_testcase_util.EVENT_TYPE_KEY_UP

WATCH_TO_BROWSE_EVENT_NAME = "wbWatchToBrowse"
WATCH_TO_BROWSE_EVENT_TYPE = tv_testcase_util.EVENT_TYPE_KEY_UP


class BrowseToWatchTest(tv_testcase.TvTestCase):

  class VideoFailureException(BaseException):
    """Exception thrown when MAX_VIDEO_FAILURE_COUNT is exceeded."""

  class AdvertisementFailureException(BaseException):
    """Exception thrown when MAX_SKIPPABLE_AD_COUNT is exceeded."""

  def test_simple(self):
    recorder_options = tv_testcase_event_recorder.EventRecorderOptions(
        self, BROWSE_TO_WATCH_EVENT_NAME, BROWSE_TO_WATCH_EVENT_TYPE)
    recorder_options.record_animations = False
    recorder_options.record_video = True
    browse_to_watch_recorder = tv_testcase_event_recorder.EventRecorder(
        recorder_options)

    recorder_options = tv_testcase_event_recorder.EventRecorderOptions(
        self, WATCH_TO_BROWSE_EVENT_NAME, WATCH_TO_BROWSE_EVENT_TYPE)
    recorder_options.record_animations = False
    watch_to_browse_recorder = tv_testcase_event_recorder.EventRecorder(
        recorder_options)

    failure_count = 0
    skippable_ad_count = 0

    for _ in xrange(NUM_LOAD_TV_CALLS):
      self.load_tv()

      for _ in xrange(NUM_ITERATIONS_PER_LOAD_TV_CALL):
        self.send_keys(keys.Keys.ARROW_DOWN)
        self.wait_for_processing_complete_after_focused_shelf()

        browse_to_watch_recorder.on_start_event()
        self.send_keys(keys.Keys.ENTER)

        if not self.wait_for_media_element_playing():
          failure_count += 1
          print("Video failed to play! {} events failed.".format(failure_count))
          if failure_count > MAX_VIDEO_FAILURE_COUNT:
            raise BrowseToWatchTest.VideoFailureException()

          self.send_keys(keys.Keys.ESCAPE)
          self.wait_for_processing_complete_after_focused_shelf()
          continue

        if self.skip_advertisement_if_playing():
          skippable_ad_count += 1
          print(
              "Encountered skippable ad! {} total.".format(skippable_ad_count))
          if skippable_ad_count > MAX_SKIPPABLE_AD_COUNT:
            raise BrowseToWatchTest.AdvertisementFailureException()

          self.wait_for_title_card_hidden()
          self.send_keys(keys.Keys.ESCAPE)
          self.wait_for_processing_complete_after_focused_shelf()
          continue

        browse_to_watch_recorder.on_end_event()

        # Wait for the title card hidden before sending the escape. Otherwise,
        # two escapes are required to exit the video.
        self.wait_for_title_card_hidden()

        watch_to_browse_recorder.on_start_event()
        self.send_keys(keys.Keys.ESCAPE)
        self.wait_for_processing_complete_after_focused_shelf()
        watch_to_browse_recorder.on_end_event()

    browse_to_watch_recorder.on_end_test()
    watch_to_browse_recorder.on_end_test()


if __name__ == "__main__":
  tv_testcase.main()
