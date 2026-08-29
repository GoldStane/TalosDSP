#include "audio/XrunCounter.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("XrunCounter starts at zero") {
  XrunCounter c;
  REQUIRE(c.total() == 0);
  REQUIRE(c.steadyTotal() == 0);
  REQUIRE(c.inputOverflow() == 0);
  REQUIRE(c.outputUnderflow() == 0);
  REQUIRE(c.inputUnderflow() == 0);
  REQUIRE(c.outputOverflow() == 0);
}

TEST_CASE("XrunCounter counts each flag independently") {
  XrunCounter c;
  c.record(true, false, false, false, false);
  REQUIRE(c.inputOverflow() == 1);
  REQUIRE(c.steadyTotal() == 1);
  REQUIRE(c.total() == 1);

  c.record(false, true, false, false, false);
  REQUIRE(c.outputUnderflow() == 1);
  REQUIRE(c.total() == 2);

  c.record(true, true, true, true, false);
  REQUIRE(c.inputOverflow() == 2);
  REQUIRE(c.outputUnderflow() == 2);
  REQUIRE(c.inputUnderflow() == 1);
  REQUIRE(c.outputOverflow() == 1);
  REQUIRE(c.total() == 6);
  REQUIRE(c.steadyTotal() == 6);
}

TEST_CASE("XrunCounter ignores blocks without flags") {
  XrunCounter c;
  for (int i = 0; i < 1000; ++i) {
    c.record(false, false, false, false, false);
  }
  REQUIRE(c.total() == 0);
  REQUIRE(c.steadyTotal() == 0);
}

TEST_CASE("XrunCounter buckets startup-grace xruns separately") {
  XrunCounter c;
  c.record(true, false, false, false, true);
  c.record(false, true, false, false, true);

  REQUIRE(c.total() == 2);
  REQUIRE(c.steadyTotal() == 0);
  REQUIRE(c.inputOverflow() == 1);
  REQUIRE(c.steadyInputOverflow() == 0);
  REQUIRE(c.outputUnderflow() == 1);
  REQUIRE(c.steadyOutputUnderflow() == 0);

  c.record(true, false, false, false, false);
  REQUIRE(c.total() == 3);
  REQUIRE(c.steadyTotal() == 1);
  REQUIRE(c.steadyInputOverflow() == 1);
}