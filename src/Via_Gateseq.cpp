#include "gateseq.hpp"
#include "Via_Graphics.hpp"


struct Via_Gateseq : Module {
    
    
    enum ParamIds {
        KNOB1_PARAM,
        KNOB2_PARAM,
        KNOB3_PARAM,
        A_PARAM,
        B_PARAM,
        CV2AMT_PARAM,
        CV3AMT_PARAM,
        BUTTON1_PARAM,
        BUTTON2_PARAM,
        BUTTON3_PARAM,
        BUTTON4_PARAM,
        BUTTON5_PARAM,
        BUTTON6_PARAM,
        TRIGBUTTON_PARAM,
        NUM_PARAMS
    };
    enum InputIds {
        A_INPUT,
        B_INPUT,
        CV1_INPUT,
        CV2_INPUT,
        CV3_INPUT,
        MAIN_LOGIC_INPUT,
        AUX_LOGIC_INPUT,
        NUM_INPUTS
    };
    enum OutputIds {
        MAIN_OUTPUT,
        LOGICA_OUTPUT,
        AUX_DAC_OUTPUT,
        AUX_LOGIC_OUTPUT,
        NUM_OUTPUTS
    };
    enum LightIds {
        LED1_LIGHT,
        LED2_LIGHT,
        LED3_LIGHT,
        LED4_LIGHT,
        OUTPUT_GREEN_LIGHT,
        OUTPUT_RED_LIGHT,
        RED_LIGHT,
        GREEN_LIGHT,
        BLUE_LIGHT,
        PURPLE_LIGHT,
        NUM_LIGHTS
    };
    
    Via_Gateseq() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {}
    void step() override;

    ViaGateseq virtualModule;
    
    int32_t lastTrigInput;
    int32_t lastAuxTrigInput;
    int32_t lastTrigButton;

    int32_t dacReadIndex = 0;
    int32_t adcWriteIndex = 0;
    int32_t slowIOPrescaler = 0;

    int32_t ledAState = 0;
    int32_t ledBState = 0;
    int32_t ledCState = 0;
    int32_t ledDState = 0;

    int32_t logicAState = 0;
    int32_t auxLogicState = 0;

    int32_t shAControl = 0;
    int32_t shBControl = 0;

    float shALast = 0;
    float shBLast = 0;

    float aSample = 0;
    float bSample = 0;

    void updateSlowIO(void) {

        virtualModule.button1Input = (int32_t) params[BUTTON1_PARAM].value;
        virtualModule.button2Input = (int32_t) params[BUTTON2_PARAM].value;
        virtualModule.button3Input = (int32_t) params[BUTTON3_PARAM].value;
        virtualModule.button4Input = (int32_t) params[BUTTON4_PARAM].value;
        virtualModule.button5Input = (int32_t) params[BUTTON5_PARAM].value;
        virtualModule.button6Input = (int32_t) params[BUTTON6_PARAM].value;

        // these have a janky array ordering to correspond with the DMA stream on the hardware
        virtualModule.controls.controlRateInputs[2] = (int32_t) params[KNOB1_PARAM].value;
        virtualModule.controls.controlRateInputs[3] = (int32_t) params[KNOB2_PARAM].value;
        virtualModule.controls.controlRateInputs[1] = (int32_t) params[KNOB3_PARAM].value;
        // model the the 1v/oct input, scale 10.6666666 volts 12 bit adc range
        // it the gain scaling stage is inverting
        float cv1Conversion = -inputs[CV1_INPUT].value;
        // ultimately we want a volt to be a chage of 384 in the adc reading
        cv1Conversion = cv1Conversion * 384.0;
        // offset to unipolar
        cv1Conversion += 2048.0;
        // clip at rails of the input opamp
        virtualModule.controls.controlRateInputs[0] = clamp((int32_t)cv1Conversion, 0, 4095);
    }

    // 2 sets the "GPIO" high, 1 sets it low, 0 is a no-op
    inline int32_t virtualLogicOut(int32_t logicOut, int32_t control) {
        return clamp(logicOut + (control & 2) - (control & 1), 0, 1);
    }

    void updateLEDs(void) {

        // the A B C D enumeration of the LEDs in the Via library makes little to no sense 
        // but its woven pretty deep so is a nagging style thing to fix

        ledAState = virtualLogicOut(ledAState, virtualModule.ledAOutput);
        ledBState = virtualLogicOut(ledBState, virtualModule.ledBOutput);
        ledCState = virtualLogicOut(ledCState, virtualModule.ledCOutput);
        ledDState = virtualLogicOut(ledDState, virtualModule.ledDOutput);

        lights[LED1_LIGHT].setBrightnessSmooth(ledAState, 2);
        lights[LED3_LIGHT].setBrightnessSmooth(ledBState, 2);
        lights[LED2_LIGHT].setBrightnessSmooth(ledCState, 2);
        lights[LED4_LIGHT].setBrightnessSmooth(ledDState, 2);

        lights[RED_LIGHT].setBrightnessSmooth(virtualModule.redLevel/4095.0, 2);
        lights[GREEN_LIGHT].setBrightnessSmooth(virtualModule.greenLevel/4095.0, 2);
        lights[BLUE_LIGHT].setBrightnessSmooth(virtualModule.blueLevel/4095.0, 2);

        float output = outputs[MAIN_OUTPUT].value/8.0;
        lights[OUTPUT_RED_LIGHT].setBrightnessSmooth(clamp(-output, 0.0, 1.0));
        lights[OUTPUT_GREEN_LIGHT].setBrightnessSmooth(clamp(output, 0.0, 1.0));

    }

    void updateLogicOutputs(void) {
        logicAState = virtualLogicOut(logicAState, virtualModule.aLogicOutput);
        auxLogicState = virtualLogicOut(auxLogicState, virtualModule.auxLogicOutput);
        shAControl = virtualLogicOut(shAControl, virtualModule.shAOutput);
        shBControl = virtualLogicOut(shBControl, virtualModule.shBOutput);
    }


    json_t *toJson() override {

        json_t *rootJ = json_object();
        
        json_object_set_new(rootJ, "gateseq_modes", json_integer(virtualModule.gateseqUI.modeStateBuffer));
        
        return rootJ;
    }
    
    void fromJson(json_t *rootJ) override {

        json_t *modesJ = json_object_get(rootJ, "gateseq_modes");
        virtualModule.gateseqUI.modeStateBuffer = json_integer_value(modesJ);
        virtualModule.gateseqUI.loadFromEEPROM(0);
        virtualModule.gateseqUI.recallModuleState();


    }
    
};

void Via_Gateseq::step() {

    // update the "slow IO" (not audio rate) every 16 samples
    // needs to scale with sample rate somehow
    slowIOPrescaler++;
    if (slowIOPrescaler == 16) {
        slowIOPrescaler = 0;
        updateSlowIO();
        virtualModule.slowConversionCallback();
        virtualModule.ui_dispatch(SENSOR_EVENT_SIG);
        virtualModule.gateseqUI.incrementTimer();
        // trigger handling
        int32_t trigButton = clamp((int32_t)params[TRIGBUTTON_PARAM].value, 0, 1);
        if (trigButton > lastTrigButton) {
            virtualModule.mainRisingEdgeCallback();
        } else if (trigButton < lastTrigButton) {
            virtualModule.mainFallingEdgeCallback();
        } 
        lastTrigButton = trigButton;
    }

    // manage the software timers
    virtualModule.sequencer.virtualTimer1Count += virtualModule.sequencer.virtualTimer1Enable;

    virtualModule.sequencer.virtualTimer2Count += virtualModule.sequencer.virtualTimer2Enable;
    virtualModule.sequencer.virtualTimer3Count += virtualModule.sequencer.virtualTimer3Enable;
    virtualModule.sequencer.virtualTimer4Count += virtualModule.sequencer.virtualTimer4Enable;

    if (virtualModule.sequencer.virtualTimer2Count >= virtualModule.sequencer.virtualTimer2Overflow) {
        virtualModule.auxTimer1InterruptCallback();
        virtualModule.sequencer.virtualTimer2Count = 0;
    }
    if (virtualModule.sequencer.virtualTimer3Count >= virtualModule.sequencer.virtualTimer3Overflow) {
        virtualModule.auxTimer2InterruptCallback();
        virtualModule.sequencer.virtualTimer3Count = 0;
    }
    if (virtualModule.sequencer.virtualTimer4Count >= virtualModule.sequencer.virtualTimer4Overflow) {
        virtualModule.auxTimer3InterruptCallback();
        virtualModule.sequencer.virtualTimer4Count = 0;
    }

    // manage audio rate dacs and adcs

    // scale -5 - 5 V to -1 to 1 and then convert to 16 bit int;
    float cv2Scale = (32767.0 * clamp(-inputs[CV2_INPUT].value/5, -1.0, 1.0)) * params[CV2AMT_PARAM].value;
    float cv3Scale = (32767.0 * clamp(-inputs[CV3_INPUT].value/5, -1.0, 1.0)) * params[CV3AMT_PARAM].value;
    int16_t cv2Conversion = (int16_t) cv2Scale;
    int16_t cv3Conversion = (int16_t) cv3Scale;

    // no ADC buffer for now..
    virtualModule.inputs.cv2Samples[0] = cv2Conversion;
    virtualModule.inputs.cv3Samples[0] = cv3Conversion;

    // trigger handling

    int32_t auxTrigInput = clamp((int32_t)inputs[AUX_LOGIC_INPUT].value, 0, 1);
    if (auxTrigInput > lastAuxTrigInput) {
        virtualModule.auxRisingEdgeCallback();
    } else if (auxTrigInput < lastAuxTrigInput) {
        virtualModule.auxFallingEdgeCallback();
    }
    lastAuxTrigInput = auxTrigInput; 

    int32_t trigInput = clamp((int32_t)inputs[MAIN_LOGIC_INPUT].value, 0, 1);
    if (trigInput > lastTrigInput) {
        virtualModule.mainRisingEdgeCallback();
    } else if (trigInput < lastTrigInput) {
        virtualModule.mainFallingEdgeCallback();
    }
    lastTrigInput = trigInput; 



    // buffer length of 1 ..
    float dac1Sample = (float) virtualModule.outputs.dac1Samples[0];
    float dac2Sample = (float) virtualModule.outputs.dac2Samples[0];
    float dac3Sample = (float) virtualModule.outputs.dac3Samples[0];
    updateLogicOutputs();
    virtualModule.halfTransferCallback();


    // "model" the circuit
    // A and B inputs with normalled reference voltages
    float aIn = inputs[A_INPUT].value + (!inputs[A_INPUT].active) * params[A_PARAM].value;
    float bIn = (inputs[B_INPUT].active) * ((inputs[B_INPUT].value) * (params[B_PARAM].value)) + (!inputs[B_INPUT].active) * (5* (params[B_PARAM].value));
    
    // sample and holds
    // get a new sample on the rising edge at the sh control output
    if (shAControl > shALast) {
        aSample = aIn;
    }
    if (shBControl > shBLast) {
        bSample = bIn;
    }

    shALast = shAControl;
    shBLast = shBControl;

    // either use the sample or track depending on the sh control output
    aIn = shAControl * aSample + !shAControl * aIn;
    bIn = shBControl * bSample + !shBControl * bIn;

    // VCA/mixing stage
    // normalize 12 bits to 0-1
    outputs[MAIN_OUTPUT].value = bIn*(dac2Sample/4095.0) + aIn*(dac1Sample/4095.0); 
    outputs[AUX_DAC_OUTPUT].value = (dac3Sample/4095.0 - .5) * -10.666666666;
    outputs[LOGICA_OUTPUT].value = logicAState * 5.0;
    outputs[AUX_LOGIC_OUTPUT].value = auxLogicState * 5.0;

    updateLEDs();
    
}

struct GateseqAux2ModeHandler : MenuItem {
    Via_Gateseq *module;
    int32_t mode;
    void onAction(EventAction &e) override {
        module->virtualModule.gateseqUI.aux2Mode = mode;
        module->virtualModule.handleAux2ModeChange(mode);
    }
};

struct GateseqRestorePresets : MenuItem {
    Via_Gateseq *module;
    ModuleWidget *moduleWidget;

    int32_t mode;
    void onAction(EventAction &e) override {
        uint32_t currentState = module->virtualModule.gateseqUI.modeStateBuffer;
        moduleWidget->reset();
        module->virtualModule.gateseqUI.modeStateBuffer = module->virtualModule.gateseqUI.stockPreset1;
        moduleWidget->save("presets/Via Gateseq 1 (Euclidean).vcvm");
        module->virtualModule.gateseqUI.modeStateBuffer = module->virtualModule.gateseqUI.stockPreset2;
        moduleWidget->save("presets/Via Gateseq 2 (2vs3).vcvm");
        module->virtualModule.gateseqUI.modeStateBuffer = module->virtualModule.gateseqUI.stockPreset3;
        moduleWidget->save("presets/Via Gateseq 3 (ShuffleSwing).vcvm");
        module->virtualModule.gateseqUI.modeStateBuffer = module->virtualModule.gateseqUI.stockPreset4;
        moduleWidget->save("presets/Via Gateseq 4 (Multiplier).vcvm");
        module->virtualModule.gateseqUI.modeStateBuffer = module->virtualModule.gateseqUI.stockPreset5;
        moduleWidget->save("presets/Via Gateseq 5 (Logic).vcvm");
        module->virtualModule.gateseqUI.modeStateBuffer = module->virtualModule.gateseqUI.stockPreset6;
        moduleWidget->save("presets/Via Gateseq 6 (SH).vcvm");
        module->virtualModule.gateseqUI.modeStateBuffer = currentState;

    }
};

struct Via_Gateseq_Widget : ModuleWidget  {

    Via_Gateseq_Widget(Via_Gateseq *module) : ModuleWidget(module) {

        box.size = Vec(12 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT);

        {
            SVGPanel *panel = new SVGPanel();
            panel->box.size = box.size;
            panel->setBackground(SVG::load(assetPlugin(plugin, "res/gateseq.svg")));
            addChild(panel);
        }
        addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        addParam(ParamWidget::create<ViaSifamBlack>(Vec(9.022 + .753, 30.90), module, Via_Gateseq::KNOB1_PARAM, 0, 4095.0, 2048.0));
        addParam(ParamWidget::create<ViaSifamBlack>(Vec(68.53 + .753, 30.90), module, Via_Gateseq::KNOB2_PARAM, 0, 4095.0, 2048.0));
        addParam(ParamWidget::create<ViaSifamBlack>(Vec(68.53 + .753, 169.89), module, Via_Gateseq::KNOB3_PARAM, 0, 4095.0, 2048.0));
        addParam(ParamWidget::create<ViaSifamGrey>(Vec(9.022 + .753, 169.89), module, Via_Gateseq::B_PARAM, -1.0, 1.0, 1.0));
        addParam(ParamWidget::create<ViaSifamBlack>(Vec(128.04 + .753, 30.90), module, Via_Gateseq::CV2AMT_PARAM, 0, 1.0, 1.0));
        addParam(ParamWidget::create<ViaSifamGrey>(Vec(128.04 + .753, 100.4), module, Via_Gateseq::A_PARAM, -5.0, 5.0, -5.0));
        addParam(ParamWidget::create<ViaSifamBlack>(Vec(128.04 + .753, 169.89), module, Via_Gateseq::CV3AMT_PARAM, 0, 1.0, 1.0));
        
        addParam(ParamWidget::create<SH_Button>(Vec(10.5 + .753, 80), module, Via_Gateseq::BUTTON1_PARAM, 0.0, 1.0, 0.0));
        addParam(ParamWidget::create<Up_Button>(Vec(47 + .753, 77.5), module, Via_Gateseq::BUTTON2_PARAM, 0.0, 1.0, 0.0));
        addParam(ParamWidget::create<Freq_Button>(Vec(85 + .753, 80), module, Via_Gateseq::BUTTON3_PARAM, 0.0, 1.0, 0.0));
        addParam(ParamWidget::create<Trig_Button>(Vec(10.5 + .753, 129), module, Via_Gateseq::BUTTON4_PARAM, 0.0, 1.0, 0.0));
        addParam(ParamWidget::create<Down_Button>(Vec(46 + .753, 131.5), module, Via_Gateseq::BUTTON5_PARAM, 0.0, 1.0, 0.0));
        addParam(ParamWidget::create<Loop_Button>(Vec(85 + .753, 129), module, Via_Gateseq::BUTTON6_PARAM, 0.0, 1.0, 0.0));
        
        addParam(ParamWidget::create<VIA_manual_button>(Vec(132.7 + .753, 320), module, Via_Gateseq::TRIGBUTTON_PARAM, 0.0, 5.0, 0.0));

        addInput(Port::create<ViaJack>(Vec(8.07 + 1.053, 241.12), Port::INPUT, module, Via_Gateseq::A_INPUT));
        addInput(Port::create<ViaJack>(Vec(8.07 + 1.053, 282.62), Port::INPUT, module, Via_Gateseq::B_INPUT));
        addInput(Port::create<ViaJack>(Vec(8.07 + 1.053, 324.02), Port::INPUT, module, Via_Gateseq::MAIN_LOGIC_INPUT));
        addInput(Port::create<ViaJack>(Vec(45.75 + 1.053, 241.12), Port::INPUT, module, Via_Gateseq::CV1_INPUT));
        addInput(Port::create<ViaJack>(Vec(45.75 + 1.053, 282.62), Port::INPUT, module, Via_Gateseq::CV2_INPUT));
        addInput(Port::create<ViaJack>(Vec(45.75 + 1.053, 324.02), Port::INPUT, module, Via_Gateseq::CV3_INPUT));
        addInput(Port::create<ViaJack>(Vec(135 + 1.053, 282.62), Port::INPUT, module, Via_Gateseq::AUX_LOGIC_INPUT));

        addOutput(Port::create<ViaJack>(Vec(83.68 + 1.053, 241.12), Port::OUTPUT, module, Via_Gateseq::LOGICA_OUTPUT));
        addOutput(Port::create<ViaJack>(Vec(83.68 + 1.053, 282.62), Port::OUTPUT, module, Via_Gateseq::AUX_DAC_OUTPUT));
        addOutput(Port::create<ViaJack>(Vec(83.68 + 1.053, 324.02), Port::OUTPUT, module, Via_Gateseq::MAIN_OUTPUT));
        addOutput(Port::create<ViaJack>(Vec(135 + 1.053, 241.12), Port::OUTPUT, module, Via_Gateseq::AUX_LOGIC_OUTPUT));

        addChild(ModuleLightWidget::create<MediumLight<WhiteLight>>(Vec(35.8 + .753, 268.5), module, Via_Gateseq::LED1_LIGHT));
        addChild(ModuleLightWidget::create<MediumLight<WhiteLight>>(Vec(73.7 + .753, 268.5), module, Via_Gateseq::LED2_LIGHT));
        addChild(ModuleLightWidget::create<MediumLight<WhiteLight>>(Vec(35.8 + .753, 309.9), module, Via_Gateseq::LED3_LIGHT));
        addChild(ModuleLightWidget::create<MediumLight<WhiteLight>>(Vec(73.7 + .753, 309.9), module, Via_Gateseq::LED4_LIGHT));
        addChild(ModuleLightWidget::create<MediumLight<GreenRedLight>>(Vec(54.8 + .753, 179.6), module, Via_Gateseq::OUTPUT_GREEN_LIGHT));
        addChild(ModuleLightWidget::create<LargeLight<RGBTriangle>>(Vec(59 + .753, 221), module, Via_Gateseq::RED_LIGHT));

        }

    void appendContextMenu(Menu *menu) override {
        Via_Gateseq *module = dynamic_cast<Via_Gateseq*>(this->module);
        assert(module);

        menu->addChild(construct<MenuLabel>());
        menu->addChild(construct<MenuLabel>(&MenuLabel::text, "Logic Mode"));
        menu->addChild(construct<GateseqAux2ModeHandler>(&MenuItem::text, "And", &GateseqAux2ModeHandler::module, module, &GateseqAux2ModeHandler::mode, 0));
        menu->addChild(construct<GateseqAux2ModeHandler>(&MenuItem::text, "Or", &GateseqAux2ModeHandler::module, module, &GateseqAux2ModeHandler::mode, 1));
        menu->addChild(construct<GateseqAux2ModeHandler>(&MenuItem::text, "Xor", &GateseqAux2ModeHandler::module, module, &GateseqAux2ModeHandler::mode, 2));
        menu->addChild(construct<GateseqAux2ModeHandler>(&MenuItem::text, "Nor", &GateseqAux2ModeHandler::module, module, &GateseqAux2ModeHandler::mode, 3));

        menu->addChild(MenuEntry::create());
        GateseqRestorePresets *restorePresets = new GateseqRestorePresets();
        restorePresets->text = "Restore presets";
        restorePresets->module = module;
        restorePresets->moduleWidget = this;
        menu->addChild(restorePresets);

        }

};



Model *modelVia_Gateseq = Model::create<Via_Gateseq, Via_Gateseq_Widget>(
        "Starling", "GATESEQ", "GATESEQ", OSCILLATOR_TAG);


