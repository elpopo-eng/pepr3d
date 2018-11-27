#include "ui/MainApplication.h"

#include "IconsMaterialDesign.h"
#include "LightTheme.h"

#include "commands/ExampleCommand.h"
#include "geometry/Geometry.h"

#include "tools/Brush.h"
#include "tools/DisplayOptions.h"
#include "tools/Information.h"
#include "tools/LiveDebug.h"
#include "tools/PaintBucket.h"
#include "tools/Segmentation.h"
#include "tools/Settings.h"
#include "tools/TextEditor.h"
#include "tools/Tool.h"
#include "tools/TrianglePainter.h"

#if defined(CINDER_MSW_DESKTOP)
#include "windows.h"
#endif

namespace pepr3d {

// At least 2 threads in thread pool must be created, or importing will never finish!
// std::thread::hardware_concurrency() may return 0
MainApplication::MainApplication()
    : mToolbar(*this),
      mSidePane(*this),
      mModelView(*this),
      mThreadPool(std::max<size_t>(3, std::thread::hardware_concurrency()) - 1) {}

void MainApplication::setup() {
    setWindowSize(950, 570);
    getWindow()->setTitle("Pepr3D - Unsaved project");
    setupIcon();
    gl::enableVerticalSync(true);
    disableFrameRate();

    getSignalWillResignActive().connect(bind(&MainApplication::willResignActive, this));
    getSignalDidBecomeActive().connect(bind(&MainApplication::didBecomeActive, this));

    auto uiOptions = ImGui::Options();
    std::vector<ImWchar> textRange = {0x0001, 0x00BF, 0};
    std::vector<ImWchar> iconsRange = {ICON_MIN_MD, ICON_MAX_MD, 0};
    uiOptions.fonts({make_pair<fs::path, float>(getAssetPath("fonts/SourceSansPro-SemiBold.ttf"), 1.0f * 18.0f),
                     make_pair<fs::path, float>(getAssetPath("fonts/MaterialIcons-Regular.ttf"), 1.0f * 24.0f)},
                    true);
    uiOptions.fontGlyphRanges("SourceSansPro-SemiBold", textRange);
    uiOptions.fontGlyphRanges("MaterialIcons-Regular", iconsRange);
    ImGui::initialize(uiOptions);
    applyLightTheme(ImGui::GetStyle());

    mGeometry = std::make_shared<Geometry>();
    mGeometry->loadNewGeometry(getAssetPath("models/defaultcube.stl").string(), mThreadPool);

    mCommandManager = std::make_unique<CommandManager<Geometry>>(*mGeometry);

    mTools.emplace_back(make_unique<TrianglePainter>(*this));
    mTools.emplace_back(make_unique<PaintBucket>(*this));
    mTools.emplace_back(make_unique<Brush>());
    mTools.emplace_back(make_unique<TextEditor>());
    mTools.emplace_back(make_unique<Segmentation>(*this));
    mTools.emplace_back(make_unique<DisplayOptions>(*this));
    mTools.emplace_back(make_unique<pepr3d::Settings>());
    mTools.emplace_back(make_unique<Information>());
    mTools.emplace_back(make_unique<LiveDebug>(*this));
    mCurrentToolIterator = mTools.begin();

    mModelView.setup();
}

void MainApplication::resize() {
    mModelView.resize();
}

void MainApplication::mouseDown(MouseEvent event) {
    mModelView.onMouseDown(event);
}

void MainApplication::mouseDrag(MouseEvent event) {
    mModelView.onMouseDrag(event);
}

void MainApplication::mouseUp(MouseEvent event) {
    mModelView.onMouseUp(event);
}

void MainApplication::mouseWheel(MouseEvent event) {
    mModelView.onMouseWheel(event);
}

void MainApplication::mouseMove(MouseEvent event) {
    mModelView.onMouseMove(event);
}

void MainApplication::fileDrop(FileDropEvent event) {
    if(mGeometry == nullptr || event.getFiles().size() < 1) {
        return;
    }
    openFile(event.getFile(0).string());
}

void MainApplication::openFile(const std::string& path) {
    if(mGeometryInProgress != nullptr) {
        return;  // disallow loading new geometry while another is already being loaded
    }

    std::shared_ptr<Geometry> geometry = mGeometryInProgress = std::make_shared<Geometry>();
    mProgressIndicator.setGeometryInProgress(geometry);
    mThreadPool.enqueue([geometry, path, this]() {
        geometry->loadNewGeometry(path, mThreadPool);
        dispatchAsync([path, this]() {
            mGeometry = mGeometryInProgress;
            mGeometryInProgress = nullptr;
            mGeometryFileName = path;
            mCommandManager = std::make_unique<CommandManager<Geometry>>(*mGeometry);
            getWindow()->setTitle(std::string("Pepr3D - ") + mGeometryFileName);
            mProgressIndicator.setGeometryInProgress(nullptr);
        });
    });

    for(auto& tool : mTools) {
        tool->onNewGeometryLoaded(mModelView);
    }
}

void MainApplication::saveFile(const std::string& filePath, const std::string& fileName, const std::string& fileType) {
    if(mGeometry == nullptr) {
        return;
    }

    mProgressIndicator.setGeometryInProgress(mGeometry);
    mThreadPool.enqueue([filePath, fileName, fileType, this]() {
        mGeometry->exportGeometry(filePath, fileName, fileType);
        dispatchAsync([this]() { mProgressIndicator.setGeometryInProgress(nullptr); });
    });
}

void MainApplication::update() {
#if defined(CINDER_MSW_DESKTOP)
    // on Microsoft Windows, when window is not focused, periodically check
    // if it is obscured (not visible) every 2 seconds
    if(!mIsFocused) {
        if((mShouldSkipDraw && (getElapsedFrames() % 4) == 0) || (!mShouldSkipDraw && (getElapsedFrames() % 48) == 0)) {
            if(isWindowObscured()) {
                mShouldSkipDraw = true;
                setFrameRate(2.0f);  // cannot set to 0.0f because then the window would never wake up again
            }
        }
    }
#endif
}

void MainApplication::draw() {
    if(mShouldSkipDraw) {
        return;
    }

    gl::clear(ColorA::hex(0xFCFCFC));

    if(mShowDemoWindow) {
        ImGui::ShowDemoWindow();
    }

    mToolbar.draw();
    mSidePane.draw();
    mModelView.draw();
    mProgressIndicator.draw();

    // if(mGeometryInProgress != nullptr) {
    //     std::string progressStatus =
    //         "%% render: " + std::to_string(mGeometryInProgress->getProgress().importRenderPercentage);
    //     ImGui::Text(progressStatus.c_str());
    //     progressStatus = "%% compute: " + std::to_string(mGeometryInProgress->getProgress().importComputePercentage);
    //     ImGui::Text(progressStatus.c_str());
    //     progressStatus = "%% buffers: " + std::to_string(mGeometryInProgress->getProgress().buffersPercentage);
    //     ImGui::Text(progressStatus.c_str());
    //     progressStatus = "%% aabb: " + std::to_string(mGeometryInProgress->getProgress().aabbTreePercentage);
    //     ImGui::Text(progressStatus.c_str());
    //     progressStatus = "%% polyhedron: " + std::to_string(mGeometryInProgress->getProgress().polyhedronPercentage);
    //     ImGui::Text(progressStatus.c_str());
    // }
}

void MainApplication::setupIcon() {
#if defined(CINDER_MSW_DESKTOP)
    auto dc = getWindow()->getDc();
    auto wnd = WindowFromDC(dc);
    auto icon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(101));  // see resources/Resources.rc
    SendMessage(wnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    SendMessage(wnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
#endif
}

void MainApplication::willResignActive() {
    setFrameRate(24.0f);
    mIsFocused = false;
}

void MainApplication::didBecomeActive() {
    disableFrameRate();
    mIsFocused = true;
    mShouldSkipDraw = false;
}

bool MainApplication::isWindowObscured() {
#if defined(CINDER_MSW_DESKTOP)
    auto dc = getWindow()->getDc();
    auto wnd = WindowFromDC(dc);

    if(IsIconic(wnd)) {
        return true;  // window is minimized (iconic)
    }

    RECT windowRect;
    if(GetWindowRect(wnd, &windowRect)) {
        // check if window is obscured by another window at 3 diagonal points (top left, center, bottom right):
        bool isObscuredAtDiagonal = true;
        POINT checkpoint;
        // check window top left:
        checkpoint.x = windowRect.left;
        checkpoint.y = windowRect.top;
        auto wndAtCheckpoint = WindowFromPoint(checkpoint);
        isObscuredAtDiagonal &= (wndAtCheckpoint != wnd);
        // check window center:
        checkpoint.x = windowRect.left + (windowRect.right - windowRect.left) / 2;
        checkpoint.y = windowRect.top + (windowRect.bottom - windowRect.top) / 2;
        wndAtCheckpoint = WindowFromPoint(checkpoint);
        isObscuredAtDiagonal &= (wndAtCheckpoint != wnd);
        // check window bottom right:
        checkpoint.x = windowRect.right - 1;
        checkpoint.y = windowRect.bottom - 1;
        wndAtCheckpoint = WindowFromPoint(checkpoint);
        isObscuredAtDiagonal &= (wndAtCheckpoint != wnd);
        if(isObscuredAtDiagonal) {
            return true;
        }
    }
#endif
    return false;
}

}  // namespace pepr3d