/* Copyright The KiCad Developers. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef KICAD_SCH_COMMIT_FLAGS_H
#define KICAD_SCH_COMMIT_FLAGS_H
// Recalculate connectivity and maintain normal Undo/dirty/view behavior, while
// preserving all geometry for operations which have already verified topology.
constexpr int SCH_COMMIT_NO_CLEANUP = 0x0008;
#endif
