#include "SwitchHandler.h"

extern "C"
{
    #include "stdlib.h"
}


__attribute__((unused)) static const char *TAG = "SwitchHandler";

SwitchHandler *SwitchHandler::instance = nullptr;

SwitchHandler *SwitchHandler::get_instance()
{
    return SwitchHandler::instance;
}
SwitchHandler *SwitchHandler::get_instance(const  gpio_num_t *_relay_pins, const gpio_num_t *_button_pins, const gpio_num_t *_button_leds, size_t _pin_num)
{
    if(SwitchHandler::instance == nullptr)
    {
        SwitchHandler::instance = new SwitchHandler(_relay_pins, _button_pins, _button_leds, _pin_num);
    }
    return SwitchHandler::instance;
}

SwitchHandler::SwitchHandler(const  gpio_num_t *_relay_pins, const gpio_num_t *_button_pins, const gpio_num_t *_button_leds, size_t _pin_num)
: relay_pins(_relay_pins), button_pins(_button_pins), button_leds(_button_leds), pin_num(_pin_num),  s_handler(SettingsHandler::get_instance())
{ //Beware, GPIO34-39 can only be used as input and got not pull-up/down.
  //We should primarily use this for ADC input.
  //See https://esp-idf.readthedocs.io/en/latest/api/peripherals/gpio.html for GPIO info
  //There needs to be at least pin_num number of pins in each of the given pin pointers
  this->state_buff = (switch_state *)malloc(this->pin_num * sizeof(switch_state));
  this->button_states = (button_state *)malloc(this->pin_num * sizeof(button_state));
  this->led_state_buff = (switch_state *)malloc(this->pin_num * sizeof(switch_state));

  this->setup_button_pins();
  this->setup_button_leds();
  this->setup_relay_pins();
  for(uint8_t i = 0; i < this->pin_num; i++)
  {
      this->led_state_buff[i] = off;
      button_state &state = this->button_states[i];
      state.state = idle;
      state.raw_state = false;
      state.filtered_state = false;
      state.timer = 0;
  }
  this->led_settings_lock = xSemaphoreCreateMutex();
  if(this->led_settings_lock == NULL) printf("error creating switchhandler led semaphore!\n");

  this->button_poll_timer = xTimerCreate ("btn_poll_tmr", POLL_TIME / portTICK_PERIOD_MS, pdTRUE, 0, &SwitchHandler::poll_buttons);

}

SwitchHandler::~SwitchHandler()
{
    free(this->state_buff);
    free(this->button_states);
    free(this->led_state_buff);

}
void SwitchHandler::setup_relay_pins()
{
    //Set the pins as output, and read the settings from the settingshandler
    //If no value is set, set default as off.

    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = ( gpio_int_type_t )GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set
    io_conf.pin_bit_mask = 0;
    //disable pull-down mode
    io_conf.pull_down_en = (gpio_pulldown_t)0;
    //disable pull-up mode
    io_conf.pull_up_en = (gpio_pullup_t)0;
    //configure GPIO with the given settings
    for(uint8_t i = 0; i < this->pin_num; i++)
    {
        io_conf.pin_bit_mask += 1<< (uint64_t)(this->relay_pins[i]);
        //ESP_ERROR_CHECK(gpio_set_direction(this->button_leds[i], GPIO_MODE_OUTPUT));
    }

    ESP_ERROR_CHECK( gpio_config(&io_conf) );

    char *switch_str = (char *)malloc(10);

    for(uint8_t i = 0; i < this->pin_num; i++)
    {
        //Set default value if not already set
        snprintf(switch_str, 10,  "SWITCH%d", i);
        switch_state state = off;
        ESP_ERROR_CHECK( this->s_handler->set_default_value(switch_str, (uint8_t)state) );

        //Read the saved state
        ESP_ERROR_CHECK( this->s_handler->nvs_get(switch_str, (uint8_t *)&state) );
        //Set the state
        this->set_switch_state(i, state, false);
    }
    free(switch_str);

}

void SwitchHandler::setup_button_pins()
{ //Setup all the button pins. They needs to be set as input with pull-down

    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = ( gpio_int_type_t )GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set
    io_conf.pin_bit_mask = 0;
    //enable pull-down mode
    io_conf.pull_down_en = (gpio_pulldown_t)1;
    //disable pull-up mode
    io_conf.pull_up_en = (gpio_pullup_t)0;
    //configure GPIO with the given settings
    for(uint8_t i = 0; i < this->pin_num; i++)
    {
        io_conf.pin_bit_mask += 1<< (uint64_t)(this->button_pins[i]);
    }
    ESP_ERROR_CHECK( gpio_config(&io_conf) );

}

void SwitchHandler::setup_button_leds()
{   //Setup pins as output. Their actual state will be set while calling setup_relay_pins()
    //This also means that this function must be called before setup_relay_pins().
    gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = ( gpio_int_type_t )GPIO_PIN_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set
    io_conf.pin_bit_mask = 0;
    //enable pull-down mode
    io_conf.pull_down_en = (gpio_pulldown_t)1;
    //disable pull-up mode
    io_conf.pull_up_en = (gpio_pullup_t)0;
    //configure GPIO with the given settings
    for(uint8_t i = 0; i < this->pin_num; i++)
    {
        io_conf.pin_bit_mask += 1<< (uint64_t)(this->button_leds[i]);
    }

    ESP_ERROR_CHECK( gpio_config(&io_conf) );

}

void SwitchHandler::set_switch_state(uint8_t switch_num, switch_state state, bool write_to_nvs)
{
    if(switch_num >= pin_num)
        return; //Simply ignore if not within range.

    //Set the relay state
    ESP_ERROR_CHECK(gpio_set_level(this->relay_pins[switch_num], state));
    //Set the led state
    //ESP_ERROR_CHECK(gpio_set_level(this->button_leds[switch_num], state));
    //printf("%d", this->button_leds[switch_num]);
    //Set the state in the buffer
    this->state_buff[switch_num] = state;

    //Write to nvs if requested
    if(write_to_nvs)
    {
        char *switch_str = (char *)malloc(10);
        snprintf(switch_str, 10,  "SWITCH%d", switch_num);
        ESP_ERROR_CHECK( this->s_handler->nvs_set(switch_str, (uint8_t)state) );
        free(switch_str);
    }
}
switch_state SwitchHandler::get_switch_state(uint8_t switch_num)
{   //Return the value stored in the buffer. this also means that we need to make absolutely sure that all changes of the pin
    //Is reflected in the buffer.

    if(switch_num >= pin_num)
        return invalid; //Simply ignore if not within range.

    return this->state_buff[switch_num];
}

uint8_t SwitchHandler::get_switch_count()
{
    return this->pin_num;
}

void SwitchHandler::poll_buttons(TimerHandle_t xTimer)
{
    if(SwitchHandler::instance->button_states == nullptr)

    //Pool all the buttons and update their states
    for(uint8_t i = 0; i < SwitchHandler::instance->pin_num; i++)
    {
        button_event emitted_event = SwitchHandler::instance->poll_button(i);
        SwitchHandler::instance->handle_event(emitted_event, i);
    }
    SwitchHandler::instance->handle_button_states();
    SwitchHandler::instance->handle_leds();
}


void SwitchHandler::handle_event(button_event the_event, uint8_t button_num)
{   //Handles events triggered by statechanges of the buttons.
    switch(the_event)
    {
        case no_event:
            return;
        case single_push:
            //toggle the switch
            this->set_switch_state(button_num, (switch_state)!this->get_switch_state(button_num));
        case single_release:
            return;
        case double_push:
            return;
        case double_release:
            return;
        default:
            return;
    }
    return;
}

void SwitchHandler::handle_button_states()
{
    //Handles actions which is to be taken as a result of the current state of the
    //buttons, as well as a combination of these states.

    //Count how many buttons are pressed.
    uint8_t btn_count = 0;
    uint64_t min_time = 0;
    for(uint8_t i = 0; i < this->pin_num; i++)
    {
        button_state & state = this->button_states[i];
        if(state.filtered_state == true)
        {
            btn_count += 1;
            if(state.timer <= min_time) min_time = state.timer;
        }
    }
    if(min_time > 10000)
    {
        //we change into blinking state, with a timout of 500 ms
        //And a delay between blinks of (20000 - min_time) / 20
        this->set_led_mode(blinking, 500);
        if(min_time < 20000)
        {
            this->set_led_blink_time( (20000 - min_time) / 20);
        }
    }
    return;
}
void SwitchHandler::handle_leds()
{
    //Modify the mode and timeout, and make a copy of the mode.
    xSemaphoreTake(this->led_settings_lock, 100000 / portTICK_RATE_MS);
    if(this->led_state_timeout <= POLL_TIME) this->led_state_timeout = 0;
    else this->led_state_timeout -= POLL_TIME;
    if(this->led_state_timeout == 0) this->led_mode = output;
    led_control_mode led_mode_copy = led_mode;
    xSemaphoreGive(this->led_settings_lock);

    //Set the leds depending on the mode

    switch(led_mode_copy)
    {
        case output:
            //set all the leds to the switch state
            for(uint8_t i = 0; i < this->pin_num; i++)
            {
                ESP_ERROR_CHECK(gpio_set_level(this->button_leds[i], this->state_buff[i]));
                this->led_state_buff[i] = this->state_buff[i];
            }
            break;
        case blinking:
            this->blink_counter += POLL_TIME;
            if(this->blink_counter >= blink_time)
            {
                this->blink_counter = 0;
                //Set to the reverse of the first led
                switch_state new_state = (switch_state)!this->led_state_buff[0];
                for(uint8_t i = 0; i < this->pin_num; i++)
                {
                    ESP_ERROR_CHECK(gpio_set_level(this->button_leds[i], new_state));
                    this->led_state_buff[i] = new_state;
                }
            }
            break;
        default:
            break;
    }
    return;
}

void SwitchHandler::set_led_mode(led_control_mode mode, uint64_t timeout)
{
    xSemaphoreTake(this->led_settings_lock, 100000 / portTICK_RATE_MS);
    this->led_mode = mode;
    this->led_state_timeout = timeout;
    xSemaphoreGive(this->led_settings_lock);
}
void SwitchHandler::set_led_blink_time(uint64_t blink_time_)
{
    xSemaphoreTake(this->led_settings_lock, 100000 / portTICK_RATE_MS);
    this->blink_time = blink_time_;
    xSemaphoreGive(this->led_settings_lock);
}

button_event SwitchHandler::poll_button(uint8_t button_num)
{
    //Debouncing
    bool raw_state = gpio_get_level(this->button_pins[button_num]);
    button_state &state = this->button_states[button_num];
    if(state.raw_state == raw_state)
    {
        state.timer += POLL_TIME;
        if(state.timer  >= DEBOUNCE_TIME)
            state.filtered_state = state.raw_state;
    }
    else state.timer = 0;
    
    state.raw_state = raw_state;


    switch(state.state)
    {
        case idle:
            if(state.filtered_state == true)
            {
                state.state = first_push;
                return button_event::single_push;
            }
            break;
        case first_push:
            if(state.filtered_state == false)
            {
                state.state = first_release;
                return button_event::single_release;
            }
            break;
        case first_release:
            if(state.filtered_state == false and state.timer >= DOUBLECLICK_THRESHOLD)
                state.state = idle;
            else if(state.filtered_state == true)
            {
                state.state = second_push;
                return button_event::double_push;
            }
            break;
        case second_push:
            if(state.filtered_state == false)
            {
                state.state = second_release;
                return button_event::double_release;
            }
            break;
        case second_release:
            state.state = idle;
            break;
        default:
            assert(false);
    }
    return button_event::no_event;
}
