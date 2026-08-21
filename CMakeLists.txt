cmake_minimum_required(VERSION 3.16)
project(NanoAODToolsCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ROOT REQUIRED COMPONENTS Core Hist MathCore Physics RIO Tree TreePlayer)
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
  src/helpers/MuonCorrection.cpp
  src/helpers/NloEWWeightProducer.cpp
  src/helpers/PuWeightProducer.cpp
  src/helpers/TopPtWeightProducer.cpp
  src/producers/HeavyFlavBaseProducer.cpp
  src/producers/HeavyFlavMinimalProducer.cpp
  src/producers/HeavyFlavMuonSampleProducer.cpp
  src/producers/HeavyFlavQCDSampleProducer.cpp
  src/producers/HeavyFlavZbbSampleProducer.cpp
  src/producers/HeavyFlavZmmSampleProducer.cpp
)

target_include_directories(nanoaodtools_cpp PUBLIC include)
target_link_libraries(nanoaodtools_cpp PUBLIC CMSJMECalculators ROOT::Core ROOT::Hist ROOT::MathCore ROOT::Physics ROOT::RIO ROOT::Tree ROOT::TreePlayer yaml-cpp)

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

add_executable(muon_external_equivalence tests/muon_external_equivalence.cpp)
target_link_libraries(muon_external_equivalence PRIVATE CMSJMECalculators)
add_test(
  NAME muon_external_equivalence
  COMMAND muon_external_equivalence
    /cvmfs/cms-griddata.cern.ch/cat/metadata/MUO/Run3-24CDEReprocessingFGHIPrompt-Summer24-NanoAODv15/latest/muon_scalesmearing.json.gz
    ${CMAKE_SOURCE_DIR}/data/muon-smearing/stdflat.json.gz
)
set_tests_properties(muon_external_equivalence PROPERTIES LABELS "muon;unit;systematics")

add_test(
  NAME vjets_ewk_json
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/vjets_ewk_json_test.py --source-dir ${CMAKE_SOURCE_DIR}
)
set_tests_properties(vjets_ewk_json PROPERTIES LABELS "vjets;correctionlib;reference")

add_test(
  NAME condor_sample_name
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/condor_sample_name_test.py --source-dir ${CMAKE_SOURCE_DIR}
)
set_tests_properties(condor_sample_name PROPERTIES LABELS "condor;runtime")
