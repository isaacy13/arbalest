find_package(Qt5 COMPONENTS Widgets Sql REQUIRED)
find_package(OpenGL REQUIRED)

set(arbalest_Include_Dirs
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "src/windows"
        "include"
        ${BRLCAD_INCLUDE_DIRS}
        ${BRLCAD_BASE_DIR}/include
        ${RT3_SOURCE_DIR}/include/brlcad)

set(arbalest_Sources
        src/gui/MainWindow.cpp
        src/main.cpp
        src/Document.cpp
        src/ObjectTree.cpp
        src/gui/ObjectTreeWidget.cpp
        src/display/GeometryRenderer.cpp
        src/display/OrthographicCamera.cpp
        src/display/PerspectiveCamera.cpp
        src/display/Display.cpp
        src/display/DisplayManager.cpp
        src/display/AxesRenderer.cpp
        src/Globals.cpp
        src/utils/QSSPreprocessor.cpp
        src/gui/Dockable.cpp
        src/gui/Properties.cpp
        src/gui/CollapsibleWidget.cpp
        src/gui/TypeSpecificProperties.cpp
        src/gui/QHBoxWidget.cpp
        src/gui/QVBoxWidget.cpp
        src/gui/DataRow.cpp
        src/gui/DragEditLineEdit.cpp
        src/gui/ObjectTreeRowButtons.cpp
        src/utils/Utils.cpp
        src/gui/DisplayGrid.cpp
        src/gui/AboutWindow.cpp
        src/display/RaytraceView.cpp
        src/gui/HelpWidget.cpp
        src/gui/MatrixTransformWidget.cpp
        src/utils/VerificationValidation.cpp
        src/utils/VerificationValidationParser.cpp
        src/gui/VerificationValidationWidget.cpp
        src/gui/MgedWidget.cpp
        src/display/GridRenderer.cpp
        src/display/MouseAction.cpp
        src/display/MoveCameraMouseAction.cpp)

IF (WIN32)
set(arbalest_Link_Libraries
        libged
        coreinterface
        Qt5::Widgets
        Qt5::Sql
        OpenGL::GL)
ELSE()
set(arbalest_Link_Libraries
        ged
        coreinterface
        Qt5::Widgets
        Qt5::Sql
        OpenGL::GL)
ENDIF()

# Meta-Object Compiling
file(GLOB arbalest_HEADERS_TO_MOC ./include/*)
qt5_wrap_cpp(arbalest_PROCESSED_MOCS  ${arbalest_HEADERS_TO_MOC} TARGET arbalest OPTIONS --no-notes)

# Resource Compiling
qt5_add_resources(arbalest_PROCESSED_RCC resources/resources.qrc)

link_directories(${BRLCAD_BASE_DIR}/lib)
include_directories(${arbalest_Include_Dirs})

add_executable(arbalest
        ${arbalest_Sources}
        ${arbalest_PROCESSED_MOCS}
        ${arbalest_PROCESSED_RCC})

target_link_libraries(arbalest ${arbalest_Link_Libraries})

set_property(TARGET arbalest PROPERTY CXX_STANDARD 17)
set_property(TARGET arbalest PROPERTY CXX_STANDARD_REQUIRED ON)