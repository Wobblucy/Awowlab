cmake_minimum_required(VERSION 3.12)
project(AnnotationManager LANGUAGES CXX)

find_package(Vulkan REQUIRED)

add_library(AnnotationManager STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnnotationManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnnotationSerializer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnnotationRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnnotationMotionCalculator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnnotationVulkanSetup.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AnnotationWorldMarkerRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UIAnnotationContextMenu.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UIAnnotationPropertiesPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UIAnnotationListPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AutoAnnotationTriggerManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UIAutoAnnotationPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UIAutoAnnotationPanel_CreatureList.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UIAutoAnnotationPanel_TriggerConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UIAutoAnnotationPanel_ActiveTriggers.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AutoAnnotationWizardState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UIAutoAnnotationWizard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/UIAutoAnnotationWizard_Steps.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Geometry/AnnotationCircleGeometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Geometry/AnnotationRectangleGeometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Geometry/AnnotationConeGeometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Geometry/AnnotationDonutGeometry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TriggerEvaluators/TriggerEvaluatorCommon.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TriggerEvaluators/SpellCastTriggerEvaluator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TriggerEvaluators/DamageTriggerEvaluator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TriggerEvaluators/DamageTakenTriggerEvaluator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TriggerEvaluators/AuraTriggerEvaluator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TriggerEvaluators/ActorTriggerEvaluator.cpp
)

target_include_directories(AnnotationManager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        # Header-only use of UI/ISpellIconRenderer.h (no link dependency on UIManager)
        ${CMAKE_CURRENT_SOURCE_DIR}/../UIManager/include
)

# No link dependency on UIManager; Camera3D is reached through ICamera3D
target_link_libraries(AnnotationManager
    PUBLIC
        Vulkan::Vulkan
        imgui
        Database
        AssetManager
)

target_precompile_headers(AnnotationManager PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../pch.h>"
)

# Unity build for faster compilation
set_target_properties(AnnotationManager PROPERTIES
    UNITY_BUILD ON
    UNITY_BUILD_BATCH_SIZE 12
)
