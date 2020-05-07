#include "GamepadController.hpp"
#include <algorithm>

using namespace std;

namespace {
constexpr auto Dribble_Step_Time = RJ::Seconds(0.125);
constexpr auto Kicker_Step_Time = RJ::Seconds(0.125);
const float AXIS_MAX = 32768.0f;
// cutoff for counting triggers as 'on'
const float TRIGGER_CUTOFF = 0.9;
}

std::vector<int> GamepadController::controllersInUse = {};
int GamepadController::deviceRemoved = -1;

GamepadController::GamepadController()
    : _controller(nullptr), _lastDribblerTime(), _lastKickerTime() {

    controllerId = -1;
    robotId = -1;
}

GamepadController::GamepadController(SDL_Event& event)
    : _controller(nullptr), _lastDribblerTime(), _lastKickerTime() {

    controllerId = -1;
    robotId = -1;
    openInputDevice(event);
}

GamepadController::~GamepadController() {
    QMutexLocker(&mutex());
    SDL_GameControllerClose(_controller);
    _controller = nullptr;
    SDL_Quit();
}

/* void GamepadController::initDeviceType() {
  if (SDL_Init(SDL_INIT_GAMECONTROLLER) != 0) {
    cerr << "SDL could not initialize! SDL Error: " << SDL_GetError()
         << endl;
    return;
  }
} */

bool GamepadController::valid() const { return _controller != nullptr; }

void GamepadController::openInputDevice(SDL_Event& event) {
    if (SDL_NumJoysticks() && SDL_IsGameController(event.cdevice.which)) {
        SDL_GameController* controller;
        controller = SDL_GameControllerOpen(event.cdevice.which);

        if (controller != nullptr) {
            _controller = controller;
            cout << "Using " << SDL_GameControllerName(_controller)
                    << " game controller as controller " << endl;
        } else {
            cerr << "ERROR: Could not open controller! SDL Error: "
                    << SDL_GetError() << endl;
        }
        return;
    }
}

void GamepadController::closeInputDevice() {
    cout << "Closing " << SDL_GameControllerName(_controller) << endl;
    SDL_GameControllerClose(_controller);
    controllerId = -1;
    robotId = -1;
}

void GamepadController::update(SDL_Event& event) {
    QMutexLocker(&mutex());

    RJ::Time now = RJ::now();

    if (SDL_GameControllerGetButton(_controller,
                                    SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
        _controls.dribble = true;
    } else if (SDL_GameControllerGetAxis(_controller,
                                         SDL_CONTROLLER_AXIS_TRIGGERLEFT) /
                   AXIS_MAX >
               TRIGGER_CUTOFF) {
        _controls.dribble = false;
    }

    /*
     *  DRIBBLER POWER
     */
    if (SDL_GameControllerGetButton(_controller, SDL_CONTROLLER_BUTTON_A)) {
        if ((now - _lastDribblerTime) >= Dribble_Step_Time) {
            _controls.dribblerPower = max(_controls.dribblerPower - 0.1, 0.0);
            _lastDribblerTime = now;
        }
    } else if (SDL_GameControllerGetButton(_controller,
                                           SDL_CONTROLLER_BUTTON_Y)) {
        if ((now - _lastDribblerTime) >= Dribble_Step_Time) {
            _controls.dribblerPower = min(_controls.dribblerPower + 0.1, 1.0);
            _lastDribblerTime = now;
        }
    } else {
        // Let dribbler speed change immediately
        _lastDribblerTime = now - Dribble_Step_Time;
    }

    /*
     *  KICKER POWER
     */
    if (SDL_GameControllerGetButton(_controller, SDL_CONTROLLER_BUTTON_X)) {
        if ((now - _lastKickerTime) >= Kicker_Step_Time) {
            _controls.kickPower = max(_controls.kickPower - 0.1, 0.0);
            _lastKickerTime = now;
        }
    } else if (SDL_GameControllerGetButton(_controller,
                                           SDL_CONTROLLER_BUTTON_B)) {
        if ((now - _lastKickerTime) >= Kicker_Step_Time) {
            _controls.kickPower = min(_controls.kickPower + 0.1, 1.0);
            _lastKickerTime = now;
        }
    } else {
        _lastKickerTime = now - Kicker_Step_Time;
    }

    /*
     *  KICK TRUE/FALSE
     */
    _controls.kick = SDL_GameControllerGetButton(
        _controller, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);

    /*
     *  CHIP TRUE/FALSE
     */
    _controls.chip = SDL_GameControllerGetAxis(
                         _controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) /
                         AXIS_MAX >
                     TRIGGER_CUTOFF;

    /*
     *  VELOCITY ROTATION
     */
    // Logitech F310 Controller
    _controls.rotation = -1 * SDL_GameControllerGetAxis(
                                  _controller, SDL_CONTROLLER_AXIS_RIGHTX) /
                         AXIS_MAX;

    /*
     *  VELOCITY TRANSLATION
     */
    auto leftX =
        SDL_GameControllerGetAxis(_controller, SDL_CONTROLLER_AXIS_LEFTX) /
        AXIS_MAX;
    auto leftY =
        -SDL_GameControllerGetAxis(_controller, SDL_CONTROLLER_AXIS_LEFTY) /
        AXIS_MAX;

    Geometry2d::Point input(leftX, leftY);

    // Align along an axis using the DPAD as modifier buttons
    if (SDL_GameControllerGetButton(_controller,
                                    SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
        input.y() = -fabs(leftY);
        input.x() = 0;
    } else if (SDL_GameControllerGetButton(_controller,
                                           SDL_CONTROLLER_BUTTON_DPAD_UP)) {
        input.y() = fabs(leftY);
        input.x() = 0;
    } else if (SDL_GameControllerGetButton(_controller,
                                           SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        input.y() = 0;
        input.x() = -fabs(leftX);
    } else if (SDL_GameControllerGetButton(_controller,
                                           SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        input.y() = 0;
        input.x() = fabs(leftX);
    }

    // Floating point precision error rounding
    if (_controls.kickPower < 1e-1) _controls.kickPower = 0;
    if (_controls.dribblerPower < 1e-1) _controls.dribblerPower = 0;
    if (fabs(_controls.rotation) < 5e-2) _controls.rotation = 0;
    if (fabs(input.y()) < 5e-2) input.y() = 0;
    if (fabs(input.x()) < 5e-2) input.x() = 0;

    _controls.translation = Geometry2d::Point(input.x(), input.y());
}

InputDeviceControlValues GamepadController::getInputDeviceControlValues() {
    QMutexLocker(&mutex());
    return _controls;
}

void GamepadController::reset() {
    QMutexLocker(&mutex());
    _controls.dribble = false;
}
