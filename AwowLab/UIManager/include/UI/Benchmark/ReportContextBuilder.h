#pragma once

#include "UI/Benchmark/UIBenchmarkReportCard.h"

class SpellIconLoader;

namespace benchmark {
class BenchmarkSession;
}

namespace benchmark_ui {

// Fills a ReportContext from the session, shared by every host that shows the
// report card.
//
// The context holds raw pointers into the session, and recompare() reassigns
// the profiles behind them, so hosts rebuild it every frame rather than
// caching it.
ReportContext buildReportContext(benchmark::BenchmarkSession& session,
                                 SpellIconLoader* iconLoader);

} // namespace benchmark_ui
