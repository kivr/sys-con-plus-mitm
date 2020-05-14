#include "SwitchHDLHandler.h"
#include "ControllerHelpers.h"
#include <cmath>

SwitchHDLHandler::SwitchHDLHandler(std::unique_ptr<IController> &&controller)
    : SwitchVirtualGamepadHandler(std::move(controller))
{
}

SwitchHDLHandler::~SwitchHDLHandler()
{
    Exit();
}

Result SwitchHDLHandler::Initialize()
{

    R_TRY(m_controller->Initialize());

    if (DoesControllerSupport(m_controller->GetType(), SUPPORTS_NOTHING))
        return 0;

    R_TRY(InitHdlState());

    if (DoesControllerSupport(m_controller->GetType(), SUPPORTS_PAIRING))
    {
        R_TRY(InitOutputThread());
    }

    R_TRY(InitInputThread());

    return 0;
}

void SwitchHDLHandler::Exit()
{
    if (DoesControllerSupport(m_controller->GetType(), SUPPORTS_NOTHING))
    {
        m_controller->Exit();
        return;
    }

    ExitInputThread();
    ExitOutputThread();
    m_controller->Exit();
    ExitHdlState();
}

Result SwitchHDLHandler::InitHdlState()
{
    int i;

    for (i = 0; i < MX_NUM; i++)
    {
        m_hdlHandle[i] = 0;
        m_deviceInfo[i] = {0};
        m_hdlState[i] = {0};

        // Set the controller type to Pro-Controller, and set the npadInterfaceType.
        m_deviceInfo[i].deviceType = HidDeviceType_FullKey15;
        m_deviceInfo[i].npadInterfaceType = NpadInterfaceType_USB;
        // Set the controller colors. The grip colors are for Pro-Controller on [9.0.0+].
        ControllerConfig *config = GetController()->GetConfig();
        m_deviceInfo[i].singleColorBody = config->bodyColor.rgbaValue;
        m_deviceInfo[i].singleColorButtons = config->buttonsColor.rgbaValue;
        m_deviceInfo[i].colorLeftGrip = config->leftGripColor.rgbaValue;
        m_deviceInfo[i].colorRightGrip = config->rightGripColor.rgbaValue;

        m_hdlState[i].batteryCharge = 4; // Set battery charge to full.
        m_hdlState[i].joysticks[JOYSTICK_LEFT].dx = 0x1234;
        m_hdlState[i].joysticks[JOYSTICK_LEFT].dy = -0x1234;
        m_hdlState[i].joysticks[JOYSTICK_RIGHT].dx = 0x5678;
        m_hdlState[i].joysticks[JOYSTICK_RIGHT].dy = -0x5678;
    }

    if (GetController()->IsControllerActive())
    {
        for (i = 0; i < MX_NUM; i++)
        {
            R_SUCCEEDED(hiddbgAttachHdlsVirtualDevice(&m_hdlHandle[i], &m_deviceInfo[i]));
        }
    }

    return 0;
}
Result SwitchHDLHandler::ExitHdlState()
{
    int i;

    for (i = 0; i < MX_NUM; i++)
    {
        R_SUCCEEDED(hiddbgDetachHdlsVirtualDevice(m_hdlHandle[i]));
    }

    return 0;
}

//Sets the state of the class's HDL controller to the state stored in class's hdl.state
Result SwitchHDLHandler::UpdateHdlState()
{
    //Checks if the virtual device was erased, in which case re-attach the device
    bool isAttached;
    int i;

    for (i = 0; i < MX_NUM; i++)
    {
        if (R_SUCCEEDED(hiddbgIsHdlsVirtualDeviceAttached(m_hdlHandle[i], &isAttached)))
        {
            if (!isAttached)
                hiddbgAttachHdlsVirtualDevice(&m_hdlHandle[i], &m_deviceInfo[i]);
        }
    }

    for (i = 0; i < MX_NUM; i++)
    {
        R_SUCCEEDED(hiddbgSetHdlsState(m_hdlHandle[i], &m_hdlState[i]));
    }

    return 0;
}

void SwitchHDLHandler::FillHdlState(const NormalizedButtonData &data)
{
    int i = (int)data.triggers[0];

    // we convert the input packet into switch-specific button states
    m_hdlState[i].buttons = 0;

    m_hdlState[i].buttons |= (data.buttons[0] ? KEY_X : 0);
    m_hdlState[i].buttons |= (data.buttons[1] ? KEY_A : 0);
    m_hdlState[i].buttons |= (data.buttons[2] ? KEY_B : 0);
    m_hdlState[i].buttons |= (data.buttons[3] ? KEY_Y : 0);

    m_hdlState[i].buttons |= (data.buttons[4] ? KEY_LSTICK : 0);
    m_hdlState[i].buttons |= (data.buttons[5] ? KEY_RSTICK : 0);

    m_hdlState[i].buttons |= (data.buttons[6] ? KEY_L : 0);
    m_hdlState[i].buttons |= (data.buttons[7] ? KEY_R : 0);

    m_hdlState[i].buttons |= (data.buttons[8] ? KEY_ZL : 0);
    m_hdlState[i].buttons |= (data.buttons[9] ? KEY_ZR : 0);

    m_hdlState[i].buttons |= (data.buttons[10] ? KEY_MINUS : 0);
    m_hdlState[i].buttons |= (data.buttons[11] ? KEY_PLUS : 0);

    ControllerConfig *config = m_controller->GetConfig();

    if (config && config->swapDPADandLSTICK)
    {
        m_hdlState[i].buttons |= ((data.sticks[0].axis_y > 0.5f) ? KEY_DUP : 0);
        m_hdlState[i].buttons |= ((data.sticks[0].axis_x > 0.5f) ? KEY_DRIGHT : 0);
        m_hdlState[i].buttons |= ((data.sticks[0].axis_y < -0.5f) ? KEY_DDOWN : 0);
        m_hdlState[i].buttons |= ((data.sticks[0].axis_x < -0.5f) ? KEY_DLEFT : 0);

        float daxis_x{}, daxis_y{};

        daxis_y += data.buttons[12] ? 1.0f : 0.0f;  //DUP
        daxis_x += data.buttons[13] ? 1.0f : 0.0f;  //DRIGHT
        daxis_y += data.buttons[14] ? -1.0f : 0.0f; //DDOWN
        daxis_x += data.buttons[15] ? -1.0f : 0.0f; //DLEFT

        // clamp lefstick values to their acceptable range of values
        float real_magnitude = std::sqrt(daxis_x * daxis_x + daxis_y * daxis_y);
        float clipped_magnitude = std::min(1.0f, real_magnitude);
        float ratio = clipped_magnitude / real_magnitude;

        daxis_x *= ratio;
        daxis_y *= ratio;

        ConvertAxisToSwitchAxis(daxis_x, daxis_y, 0, &m_hdlState[i].joysticks[JOYSTICK_LEFT].dx, &m_hdlState[i].joysticks[JOYSTICK_LEFT].dy);
    }
    else
    {
        m_hdlState[i].buttons |= (data.buttons[12] ? KEY_DUP : 0);
        m_hdlState[i].buttons |= (data.buttons[13] ? KEY_DRIGHT : 0);
        m_hdlState[i].buttons |= (data.buttons[14] ? KEY_DDOWN : 0);
        m_hdlState[i].buttons |= (data.buttons[15] ? KEY_DLEFT : 0);

        ConvertAxisToSwitchAxis(data.sticks[0].axis_x, data.sticks[0].axis_y, 0, &m_hdlState[i].joysticks[JOYSTICK_LEFT].dx, &m_hdlState[i].joysticks[JOYSTICK_LEFT].dy);
    }

    ConvertAxisToSwitchAxis(data.sticks[1].axis_x, data.sticks[1].axis_y, 0, &m_hdlState[i].joysticks[JOYSTICK_RIGHT].dx, &m_hdlState[i].joysticks[JOYSTICK_RIGHT].dy);

    m_hdlState[i].buttons |= (data.buttons[16] ? KEY_CAPTURE : 0);
    m_hdlState[i].buttons |= (data.buttons[17] ? KEY_HOME : 0);
}

void SwitchHDLHandler::UpdateInput()
{
    // We process any input packets here. If it fails, return and try again
    Result rc = m_controller->GetInput();
    if (R_FAILED(rc))
        return;

    // This is a check for controllers that can prompt themselves to go inactive - e.g. wireless Xbox 360 controllers
    if (!m_controller->IsControllerActive())
    {
        int i;
        for (i = 0; i < MX_NUM; i++)
        {
            hiddbgDetachHdlsVirtualDevice(m_hdlHandle[i]);
        }
    }
    else
    {
        // We get the button inputs from the input packet and update the state of our controller
        FillHdlState(m_controller->GetNormalizedButtonData());
        rc = UpdateHdlState();
        if (R_FAILED(rc))
            return;
    }
}

void SwitchHDLHandler::UpdateOutput()
{
    // Process a single output packet from a buffer
    if (R_SUCCEEDED(m_controller->OutputBuffer()))
        return;

    // Process rumble values if supported
    if (DoesControllerSupport(m_controller->GetType(), SUPPORTS_RUMBLE))
    {
        Result rc;
        HidVibrationValue value;
        rc = hidGetActualVibrationValue(&m_vibrationDeviceHandle, &value);
        if (R_SUCCEEDED(rc))
            m_controller->SetRumble(static_cast<uint8_t>(value.amp_high * 255.0f), static_cast<uint8_t>(value.amp_low * 255.0f));
    }

    svcSleepThread(1e+7L);
}
