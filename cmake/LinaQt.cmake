# SPDX-License-Identifier: GPL-3.0-or-later
#
# Qt, from system packages (docs/design/14-build-test-and-ci.md). Found once here so that every
# tier from the bridge upwards, and their tests, see the same imported targets.
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui)
