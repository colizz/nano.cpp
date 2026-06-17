cmake_minimum_required(VERSION 3.16)
project(NanoAODToolsCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ROOT REQUIRED COMPONENTS Core MathCore Physics RIO Tree TreePlayer)
find_package(yaml-cpp REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()

set(SKBUILD ON CACHE BOOL "Disable CMSJMECalculators python/dictionary build" FORCE)
add_subdirectory(external/CMSJMECalculators EXCLUDE_FROM_ALL)
unset(SKBUILD CACHE)

add_library(nanoaodtools_cpp
  src/core/BranchSchema.cpp
  src/io/NanoReader.cpp
  src/io/RootOutputFile.cpp
  src/core/Event.cpp
  src/core/Collection.cpp
  src/core/OutputModel.cpp
  src/core/Helpers.cpp
  src/helpers/FatjetGenMatching.cpp
  src/helpers/JetMETCorrector.cpp
  src/helpers/JmeVariation.cpp
  src/helpers/PuWeightProducer.cpp
  src/helpers/TopPtWeightProducer.cpp
  src/producers/HeavyFlavBaseProducer.cpp
  src/producers/HeavyFlavMuonSampleProducer.cpp
)

target_include_directories(nanoaodtools_cpp PUBLIC include)
target_link_libraries(nanoaodtools_cpp PUBLIC CMSJMECalculators ROOT::Core ROOT::MathCore ROOT::Physics ROOT::RIO ROOT::Tree ROOT::TreePlayer yaml-cpp)

add_executable(nano_run app/nano_run.cpp)
target_link_libraries(nano_run PRIVATE nanoaodtools_cpp)

add_executable(nano_make_condor app/nano_make_condor.cpp)
target_link_libraries(nano_make_condor PRIVATE nanoaodtools_cpp)

add_executable(nano_merge app/nano_merge.cpp)
target_link_libraries(nano_merge PRIVATE nanoaodtools_cpp)

add_test(
  NAME muon_validation
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/muon_validation_test.py --source-dir ${CMAKE_SOURCE_DIR} --build-dir ${CMAKE_BINARY_DIR} --group muon --max-input-events 5000
)
set_tests_properties(muon_validation PROPERTIES TIMEOUT 7200 LABELS "muon;reference;systematics")
