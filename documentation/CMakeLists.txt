# Explore programs behind the charts in documentation/ (JP_BUILD_DOCS). Each writes plain
# text for documentation/Plot/PyConPlot.py; dev-scripts/dev-docs-plots.sh builds, runs and
# renders them.

function(jp_docs_explore target source)
    add_executable(${target} ${source})
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR})
    target_compile_options(${target} PRIVATE -O2)
    add_dependencies(docs_explore ${target})
endfunction()

add_custom_target(docs_explore)

jp_docs_explore(OrnsteinUhlenbeckExplore OrnsteinUhlenbeck/OrnsteinUhlenbeckExplore.cpp)
jp_docs_explore(PoleMixingExplore Filters/PoleMixing/PoleMixingExplore.cpp)
jp_docs_explore(ShipAhoyExplore ShipAhoy/ShipAhoyExplore.cpp)
jp_docs_explore(AnalysisExplore Analysis/AnalysisExplore.cpp)
jp_docs_explore(YinPitchExplore YinPitch/YinPitchExplore.cpp)
jp_docs_explore(EigenghostExplore Eigenghost/EigenghostExplore.cpp)
jp_docs_explore(JaysTwinExplore JaysTwin/JaysTwinExplore.cpp)
jp_docs_explore(WavetablesExplore Wavetables/WavetablesExplore.cpp)
