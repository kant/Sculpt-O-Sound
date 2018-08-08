#include "std.hpp"
#include "matrix.hpp"
#include "pan_and_level.hpp"
#include "Sculpt-O-Sound.hpp"
#include "comp_coeffs.hpp"
#include "dsp/digital.hpp"

#define INITIAL_CARRIER_GAIN 1.0
#define INITIAL_MODULATOR_GAIN 1.0

// Dimensions of matrix of buttons.
#define LED_WIDTH 10
#define LED_HEIGHT 10

#define VBASE NR_OF_BANDS * LED_HEIGHT + 40

#define ORIGIN_BOTTOM_LEFT

#define HBASE 130

#define LED_LIGHT_PARAM 4

void refresh_led_matrix(int lights_offset, int p_cnt[NR_OF_BANDS], int button_value[NR_OF_BANDS][NR_OF_BANDS], bool led_state[1024], std::vector<Light> lights) 
{
    for (int i = 0; i < NR_OF_BANDS; i++)     
    {
       for (int j = 0; j < NR_OF_BANDS; j++)
       {
           led_state[i * NR_OF_BANDS + j] = false;
           lights[lights_offset + i * NR_OF_BANDS + j].value = false;
       }
       for (int j = 0; j < p_cnt[i]; j++)
       {
           led_state[i * NR_OF_BANDS + button_value[i][j]] = true;
           lights[lights_offset + i * NR_OF_BANDS + button_value[i][j]].value = true;
       }
    }
}

struct Vocode_O_Matic_v02 : Module {

  enum ParamIds {
    // aplification factor
    DEBUG_PARAM,
    // bypass switch
    BYPASS_SWITCH,
    // toggle button to choose matrix type
    MATRIX_TYPE_TOGGLE_PARAM,
    // switch to start shift of matrix (to the right)
    MATRIX_SHIFT_TOGGLE_PARAM,
    CARRIER_GAIN_PARAM,
    MODULATOR_GAIN_PARAM,
    PANNING_PARAM,
    LED_ON_PARAM,
    NUM_PARAMS = LED_ON_PARAM + NR_OF_BANDS * NR_OF_BANDS
  };

  enum InputIds {
    // input signal
    CARR_INPUT,
    MOD_INPUT, 
    NUM_INPUTS
  };

  enum OutputIds {
    // output signal
    LEFT_OUTPUT,
    RIGHT_OUTPUT,
    NUM_OUTPUTS
  };

  enum LightIds {
    // bypass light.
    BYPASS_LIGHT,
    // matrix type light (toggles when pressed)
    MATRIX_TYPE_TOGGLE_LIGHT, 
    // matrix shift indicator light
    MATRIX_SHIFT_TOGGLE_LIGHT,
    LED_LIGHT,
    NUM_LIGHTS = LED_LIGHT + NR_OF_BANDS * NR_OF_BANDS
  };

  float blinkPhase = -1.0f;

  void step() override;

  // For more advanced Module features, read Rack's engine.hpp header file
  // - toJson, fromJson: serialization of internal data
  // - onSampleRateChange: event triggered by a change of sample rate
  // - onReset, onRandomize, onCreate, onDelete: implements special behavior when user clicks these from the context menu

  void onReset() override;
  //void onRandomize() override;

  float smoothing_factor,   
        ym[NR_OF_BANDS][3],  // filter taps (y = output, x = input )
        xm[3] = {0.0,  0.0,  0.0};
  float yc[NR_OF_BANDS][3],  // filter taps (y = output, x = input )
        xc[3] = {0.0,  0.0,  0.0};
  float xm_env = 0.0, 
        ym_env[NR_OF_BANDS][2];

  int freq[33] = {0, 20, 25, 32, 40, 50, 63, 80, 100, 125, 160, 200, 250,
               315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500,
               3150, 4000, 5000, 6300, 8000, 10000,      
               12500, 16000, 20000, 22025};
  int i = 0;
  double carr_bandwidth = INITIAL_CARR_BW_IN_SEMITONES;
  double mod_bandwidth = INITIAL_MOD_BW_IN_SEMITONES;
  double fsamp = FFSAMP;

  // Button for by pass on and off.
  SchmittTrigger bypass_button_trig;
  bool fx_bypass = false;
  // Button to toggle the filter band coupling type (4 * log)
  SchmittTrigger matrix_type_button_trig;
  bool matrix_type_button_pressed = false;
  // Start with linear coupling of filters.
  int matrix_type_selector = INITIAL_MATRIX_TYPE;
  int lcd_matrix_type = matrix_type_selector;

  // What is the shift position of the matrix.
  int lcd_matrix_shift_position = 0;

  SchmittTrigger matrix_shift_button_trig;
  bool matrix_shift_button_pressed = false;

  SchmittTrigger edit_matrix_trigger;
  bool edit_matrix_state = false;
  int wait = 1;

  int p_cnt[NR_OF_BANDS];
  int button_value[NR_OF_BANDS][NR_OF_BANDS];
  float mod_alpha1[NR_OF_BANDS];
  float mod_alpha2[NR_OF_BANDS];
  float mod_beta[NR_OF_BANDS];
  float carr_alpha1[NR_OF_BANDS];
  float carr_alpha2[NR_OF_BANDS];
  float carr_beta[NR_OF_BANDS];
  float left_pan[NR_OF_BANDS];
  float right_pan[NR_OF_BANDS];
  float left_level[NR_OF_BANDS];
  float right_level[NR_OF_BANDS];
  float start_level[NR_OF_BANDS];
  float envelope_attack_time[NR_OF_BANDS], envelope_release_time[NR_OF_BANDS]; 
  float envelope_attack_factor[NR_OF_BANDS], envelope_release_factor[NR_OF_BANDS]; 
  float width = 1.0;
  float width_old = width;
  bool led_state[1024] = {};
  int lights_offset = LED_LIGHT;

  // Some code to read/save state of bypass button.
  json_t *toJson()override {
    json_t *rootJm = json_object();
    json_t *statesJ = json_array();
    json_t *bypassJ = json_boolean(fx_bypass);
    json_array_append_new(statesJ, bypassJ);
    json_object_set_new(rootJm, "as_FxBypass", statesJ);
    return rootJm;
  }       
          
  void fromJson(json_t *rootJm)override {
    json_t *statesJ = json_object_get(rootJm, "as_FxBypass");
    json_t *bypassJ = json_array_get(statesJ, 0);
    fx_bypass = !!json_boolean_value(bypassJ);
  } 

  Vocode_O_Matic_v02() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
    //static const float RACK_GRID_WIDTH = 15;
    //static const float RACK_GRID_HEIGHT = 2 * 380;
    //static const Vec RACK_GRID_SIZE = Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT);  

    // Initialize the filter coefficients.
    comp_all_coeffs(freq, mod_bandwidth, fsamp, mod_alpha1, mod_alpha2, mod_beta);
    comp_all_coeffs(freq, carr_bandwidth, fsamp, carr_alpha1, carr_alpha2, carr_beta);
#ifdef DEBUG
    print_all_coeffs(mod_alpha1, mod_alpha2, mod_beta);
    print_all_coeffs(carr_alpha1, carr_alpha2, carr_beta);
    printf("carr_alpha1: ");
    print_array(carr_alpha1); 
    printf("carr_alpha2: ");
    print_array(carr_alpha2); 
    printf("carr_beta: ");
    print_array(carr_beta); 
#endif
  
    // Initialize all filter taps. 
    for (int i = 0; i < NR_OF_BANDS; i++) {
     for (int j = 0; j < 3; j++) { 
          ym[i][j] = 0.0;
      }
      ym_env[i][0] = 0.0; // envelope of modulator
      ym_env[i][1] = 0.0;                                                                                                                                                                                       
    }
    // Initialize the levels and pans.
    initialize_start_levels(start_level);
    init_pan_and_level(start_level, left_pan, right_pan, left_level, right_level);
    choose_matrix(4, button_value, p_cnt); // initialize linear filter coupling.
#ifdef DEBUG
    print_matrix(button_value);
    print_p_cnt(p_cnt);
#endif

    // Refresh LED matrix.
    refresh_led_matrix(lights_offset, p_cnt, button_value, led_state, lights);

    fx_bypass = false;
    blinkPhase = -1.0f;
    // Reset lights .
    lights[MATRIX_SHIFT_TOGGLE_LIGHT].value = 0.0;
    lights[BYPASS_LIGHT].value = 0.0;
  
    comp_attack_times(envelope_attack_time); 
    comp_attack_factors(envelope_attack_factor, envelope_attack_time);
  
    comp_release_times(envelope_release_time); 
    comp_release_factors(envelope_release_factor, envelope_release_time);
#ifdef DEBUG
    print_matrix(button_value);
    print_p_cnt(p_cnt);
    printf("attack times: ");
    print_array(envelope_attack_time); 
    printf("envelope_attack_factor: ");
    print_array(envelope_attack_factor);
    printf("release times: ");
    print_array(envelope_attack_time); 
    printf("envelope_release_factor: ");
    print_array(envelope_release_factor);
    print_matrix(button_value);
    print_p_cnt(p_cnt);
#endif
  } 
};

struct LButton : SVGSwitch, MomentarySwitch {
    LButton() {
        addFrame(SVG::load(assetPlugin(plugin, "res/L.svg")));
        addFrame(SVG::load(assetPlugin(plugin, "res/Ldown.svg")));
        sw->wrap();
        box.size = sw->box.size;
    }
};

void Vocode_O_Matic_v02::onReset() {
  matrix_type_button_pressed = false;
  matrix_shift_button_pressed = false;
  matrix_type_selector = INITIAL_MATRIX_TYPE;
  lcd_matrix_type = matrix_type_selector;
  lcd_matrix_shift_position = 0;
  choose_matrix(4, button_value, p_cnt); // Initialize linear filter coupling.
#ifdef DEBUG
  print_matrix(button_value);
  print_p_cnt(p_cnt);
#endif

  // Refresh LED matrix.
  refresh_led_matrix(lights_offset, p_cnt, button_value, led_state, lights);
  fx_bypass = false;
  blinkPhase = -1.0f;

  // Reset lights.
  lights[MATRIX_SHIFT_TOGGLE_LIGHT].value = 0.0;
  lights[BYPASS_LIGHT].value = 0.0;
}


void Vocode_O_Matic_v02::step() {
  // Implement the Vocoder.

  float deltaTime = engineGetSampleTime();
             
  xc[0] = inputs[CARR_INPUT].value * params[CARRIER_GAIN_PARAM].value;
  xm[0] = inputs[MOD_INPUT].value * params[MODULATOR_GAIN_PARAM].value;
  float smoothing_factor = 1.0;

  for (int i = 0; i < NR_OF_BANDS; i++) {
    //
    // CARRIER
    //  
    // Direct Form I topology is used for filtering. Direct Form II would in this case cost as many multiplications and shifts.
    yc[i][0] = carr_alpha1[i] * (xc[0] - xc[2] - carr_alpha2[i] * yc[i][1] - carr_beta[i] * yc[i][2]);
    //  
    // Shift all carrier filter taps.
    yc[i][2] = yc[i][1]; yc[i][1] = yc[i][0];

    //
    // MODULATOR
    //
    ym[i][0] = mod_alpha1[i] * (xm[0] - xm[2] - mod_alpha2[i] * ym[i][1] - mod_beta[i] * ym[i][2]);
    // 
    // Shift filter taps.
    ym[i][2] = ym[i][1]; ym[i][1] = ym[i][0];
    //  
    // Compute input for envelope for this band.
    // Use only positive values so that energy levels are zero or positive.
    xm_env = fabs(ym[i][0]);
    //
    // Perform AR averager on this band's output.
    if (ym_env[i][1] < xm_env)
        smoothing_factor = envelope_attack_factor[i];
    else
        smoothing_factor = envelope_release_factor[i];
    ym_env[i][0] = (1.0 - smoothing_factor) * xm_env + smoothing_factor * ym_env[i][1];
    //
    // Shift low pass filter taps.
    ym_env[i][1] = ym_env[i][0];
  }

  // Shift modulator input taps.
  xm[2] = xm[1]; xm[1] = xm[0];
  // 
  // Shift carrier input taps.
  xc[2] = xc[1]; xc[1] = xc[0];

  // Handling of buttons.
  if (matrix_shift_button_trig.process(params[MATRIX_SHIFT_TOGGLE_PARAM].value))
  {
    matrix_shift_button_pressed = !matrix_shift_button_pressed;
    lights[MATRIX_SHIFT_TOGGLE_LIGHT].value = matrix_shift_button_pressed ? 1.00 : 0.0;
  }
  // Blink light at 1Hz.
  blinkPhase += deltaTime;
  if (blinkPhase >= 1.0f) {
    blinkPhase -= 1.0f;
    // Shift, but not when in bypass mode.
    if ((not fx_bypass) && matrix_shift_button_pressed) {
        // Shift one step.
        matrix_shift_buttons_to_right(button_value, p_cnt);

        // Refresh matrix.
        refresh_led_matrix(lights_offset, p_cnt, button_value, led_state, lights);

        lcd_matrix_shift_position += 1;
        if (lcd_matrix_shift_position >= NR_OF_BANDS) 
            lcd_matrix_shift_position = 0;
    }
  }

  if (matrix_shift_button_pressed) { // We blink only if the button is toggled in the on position.
    lights[MATRIX_SHIFT_TOGGLE_LIGHT].value = (blinkPhase < 0.5f) ? 1.0f : 0.0f;
  }
  // 
  // If frequency matrix button was pressed.
  if (matrix_type_button_trig.process(params[MATRIX_TYPE_TOGGLE_PARAM].value))
  {
    //matrix_type_button_pressed = !matrix_type_button_pressed;
    //lights[MATRIX_TYPE_TOGGLE_PARAM].value = matrix_type_button_pressed ? 1.00 : 0.0;
    matrix_type_button_pressed = false;
    lights[MATRIX_TYPE_TOGGLE_PARAM].value = matrix_type_button_pressed ? 1.00 : 0.0;
    matrix_type_selector++; 
    if (matrix_type_selector > NR_MATRIX_TYPES - 1) { matrix_type_selector = 0; }
    choose_matrix(matrix_type_selector, button_value, p_cnt);
#ifdef DEBUG
    print_matrix(button_value);
    print_p_cnt(p_cnt);
#endif

    // Refresh LED matrix.
    refresh_led_matrix(lights_offset, p_cnt, button_value, led_state, lights);

    lcd_matrix_type = matrix_type_selector;
    // We restart the shift counter at 0.
    lcd_matrix_shift_position = 0;
  }

  // Panning
#ifdef PANNING
  width = params[PANNING_PARAM].value;
  if (width != width_old) 
  {
    set_pan_and_level(start_level, left_pan, right_pan, left_level, right_level, width);
    width_old = width;
  }
#endif

  // If a matrix button is pressed, adapt the matrix.
  if (edit_matrix_trigger.process(params[LED_ON_PARAM].value)) 
  {
    edit_matrix_state = !edit_matrix_state ;
    //lights[LED_ON_PARAM].value = ! lights[LED_ON_PARAM].value;
  }

  if (wait == 0) 
  {
    for (int i = 0; i < NR_OF_BANDS * NR_OF_BANDS; i++) 
    {
        if (params[LED_ON_PARAM + i].value) 
        {
            wait = 20000;
            led_state[i] = !led_state[i]; 
            int index = LED_LIGHT + i;
            lights[index].value = !lights[index].value;
            int chosen_row = i / NR_OF_BANDS;
            int chosen_col = i % NR_OF_BANDS;
#ifdef DEBUG 
	        printf("row %d col %d was clicked\n", chosen_row, chosen_col);
#endif
            if ((p_cnt[chosen_row] > 0) && (!led_state[i]))
            {
                for (int col = 0; col < NR_OF_BANDS; col++) 
                { // Find the right column.
#ifdef DEBUG 
		            printf("finding button_value[%d][%d] == %d\n", chosen_row, col, chosen_col);
#endif
                    if (button_value[chosen_row][col] == chosen_col)
                    {
                        button_value[chosen_row][col] = NOT_PRESSED;
			             // Shift all values from unpressed button 1 position to the left.
                        for (int shift_col = col; shift_col < p_cnt[chosen_row]; shift_col++) {
				             button_value[chosen_row][shift_col] = button_value[chosen_row][shift_col + 1];
			            }
                        p_cnt[chosen_row]--;
#ifdef DEBUG
                        printf("%d %d -> UN PRESSED\n", chosen_row, chosen_col);
                        print_matrix(button_value);
                        print_p_cnt(p_cnt);
#endif
			            break; // As soon as one button is unpressed we are done.
                    }
                }
            } 
            else 
            {
                button_value[chosen_row][p_cnt[chosen_row]] = chosen_col;
                p_cnt[chosen_row]++;
#ifdef DEBUG
                printf("%d %d -> PRESSED\n", chosen_row, chosen_col);
                print_matrix(button_value);
                print_p_cnt(p_cnt);
#endif
            }
        }
    }
  } else wait = wait - 1;


  // If bypass button was pressed:
  if (bypass_button_trig.process(params[BYPASS_SWITCH].value))
  {
    fx_bypass = !fx_bypass;
    lights[BYPASS_LIGHT].value = fx_bypass ? 1.00 : 0.0;
  }
  if (fx_bypass) {
    outputs[LEFT_OUTPUT].value = inputs[CARR_INPUT].value * params[CARRIER_GAIN_PARAM].value;
    outputs[RIGHT_OUTPUT].value = inputs[MOD_INPUT].value * params[MODULATOR_GAIN_PARAM].value;
  } 
  else 
  {
    // Initialize output signal.
    outputs[RIGHT_OUTPUT].value = 0.0;
    outputs[LEFT_OUTPUT].value = 0.0;
    for (int i = NR_OF_BANDS -1; i >= 0; i--)
    {  
      for (int j = 0; j < p_cnt[i]; j++)
      {
        int ind = button_value[i][j];
        // Use fl_tmp0 and fl_tmp to speed things up a bit.
        float fl_tmp0 = yc[ind][0] * ym_env[i][0];
        //
        // Left channel.
        float fl_tmp = fl_tmp0 * left_level[i];
        // Compute output value of signal ( superposition )
        outputs[LEFT_OUTPUT].value += fl_tmp;
        //
        // Right channel.
        fl_tmp = fl_tmp0 * right_level[i];
        // Compute output value of signal ( superposition )
        outputs[RIGHT_OUTPUT].value += fl_tmp;
      }
    }
  }
}

struct Vocode_O_Matic_v02Widget : ModuleWidget {
  Vocode_O_Matic_v02Widget(Vocode_O_Matic_v02 *module) : ModuleWidget(module) {
    setPanel(SVG::load(assetPlugin(plugin, "res/Sculpt-O-Sound-_-Vocode_O_Matic_v02.svg")));
    //setPanel(SVG::load(assetPlugin(plugin, "res/breed.svg")));

    addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
    addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    // Dial for carrier gain.
    // Dial for modulator gain.
    // Dial for panning.
    addParam(ParamWidget::create<RoundSmallBlackKnob>(Vec(10,  47), module, Vocode_O_Matic_v02::CARRIER_GAIN_PARAM, 1.0, 10.0, INITIAL_CARRIER_GAIN));
    addParam(ParamWidget::create<RoundSmallBlackKnob>(Vec(40,  47), module, Vocode_O_Matic_v02::MODULATOR_GAIN_PARAM, 1.0, 10.0, INITIAL_MODULATOR_GAIN));
#ifdef PANNING
    addParam(ParamWidget::create<RoundSmallBlackKnob>(Vec(70,  47), module, Vocode_O_Matic_v02::PANNING_PARAM, 0.0, MAX_PAN, 1.0 / INITIAL_PAN_OFFSET));
#endif

    // INTPUTS (SIGNAL AND PARAMS)
    // Input signals.
    addInput(Port::create<PJ301MPort>(Vec(10, 180), Port::INPUT, module, Vocode_O_Matic_v02::CARR_INPUT));
    addInput(Port::create<PJ301MPort>(Vec(40, 180), Port::INPUT, module, Vocode_O_Matic_v02::MOD_INPUT));

    // Bypass switch.
    addParam(ParamWidget::create<LEDBezel>(Vec(12,  90), module, Vocode_O_Matic_v02::BYPASS_SWITCH , 0.0f, 1.0f, 0.0f));
    addChild(ModuleLightWidget::create<LedLight<RedLight>>(Vec(14.2, 92), module, Vocode_O_Matic_v02::BYPASS_LIGHT));

    // Matrix type switch.
    addParam(ParamWidget::create<LEDBezel>(Vec(12, 120), module, Vocode_O_Matic_v02::MATRIX_TYPE_TOGGLE_PARAM, 0.0f, 1.0f, 0.0f));
    addChild(ModuleLightWidget::create<LedLight<GreenLight>>(Vec(14.2, 122), module, Vocode_O_Matic_v02::MATRIX_TYPE_TOGGLE_LIGHT));

    // Matrix shift toggle.
    addParam(ParamWidget::create<LEDBezel>(Vec(12, 150), module, Vocode_O_Matic_v02::MATRIX_SHIFT_TOGGLE_PARAM, 0.0f, 1.0f, 0.0f));
    addChild(ModuleLightWidget::create<LedLight<BlueLight>>(Vec(14.2, 152), module, Vocode_O_Matic_v02::MATRIX_SHIFT_TOGGLE_LIGHT));

    // MS DISPLAY                                                                  
    // Matrix type
    MsDisplayWidget2 *display1 = new MsDisplayWidget2();                            
    display1->box.pos = Vec(40, 121);                                               
    display1->box.size = Vec(30, 20);                                             
    display1->value = &module->lcd_matrix_type;
    addChild(display1);                                                           

    // MS DISPLAY                                                                  
    // Shift position
    MsDisplayWidget2 *display2 = new MsDisplayWidget2();                            
    display2->box.pos = Vec(40, 151);                                               
    display2->box.size = Vec(30, 20);                                             
    display2->value = &module->lcd_matrix_shift_position;
    addChild(display2);                                                           

    // Output for filtered signal.
    addOutput(Port::create<PJ301MPort>(Vec(10, 210), Port::OUTPUT, module, Vocode_O_Matic_v02::LEFT_OUTPUT));
    addOutput(Port::create<PJ301MPort>(Vec(40, 210), Port::OUTPUT, module, Vocode_O_Matic_v02::RIGHT_OUTPUT));

    // Matrix, origin is bottom left.
    for (int i = 0; i < NR_OF_BANDS; i++) {
        for (int j = 0; j < NR_OF_BANDS; j++) {
            int x = HBASE + j * LED_WIDTH;
            int y = VBASE - i * (LED_HEIGHT + 1);
            int offset = i * NR_OF_BANDS + j;
            addParam(ParamWidget::create<LButton>(Vec(x, y), module, Vocode_O_Matic_v02::LED_ON_PARAM + offset, 0.0, 1.0, 0.0));
            addChild(ModuleLightWidget::create<MediumLight<BlueLight>>(Vec(x, y), module, Vocode_O_Matic_v02::LED_LIGHT + offset));
        }
    }
  }
};


// Specify the Module and ModuleWidget subclass, human-readable
// author name for categorization per plugin, module slug (should never
// change), human-readable module name, and any number of tags
// (found in `include/tags.hpp`) separated by commas.
Model *modelVocode_O_Matic_v02 = Model::create<Vocode_O_Matic_v02, Vocode_O_Matic_v02Widget>("Sculpt-O-Sound", "Vocode_O_Matic_v02", "Vocode_O_Matic_v02", FILTER_TAG);
