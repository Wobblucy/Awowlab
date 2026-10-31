#include "UI/Benchmark/ReportContextBuilder.h"

#include "Core/Benchmark/BenchmarkSession.h"
#include "Core/CurrentEncounter.h"

namespace benchmark_ui {

ReportContext buildReportContext(benchmark::BenchmarkSession& session,
                                 SpellIconLoader* iconLoader) {
    ReportContext context;
    context.result = &session.result();
    context.userProfile = session.userProfile();
    context.mentorProfile = session.mentorProfile();
    context.spellNames = &session.spellNames();
    context.npcNames = &session.npcNames();
    context.userName = session.userName();
    context.mentorName = session.mentorName();
    context.encounterName = CurrentEncounter::instance().getName();
    context.profilesStale = session.needsRebuild();
    context.canRebuild = session.mentorOrigin().isValid();
    context.iconLoader = iconLoader;
    return context;
}

} // namespace benchmark_ui
