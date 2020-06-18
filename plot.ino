

void draw_safe(int x, int y, int x2, int y2){
  if (x < 0)
    x = 0;
  if (x > 127)
    x = 127;
  if (y < 0)
    y = 0;
  if (y > 63)
    y = 63;
  if (x2 > 127)
    x2 = 127;
  if (y2 > 63)
    y2 = 63;
  GLCD.DrawLine(x,y,x2,y2);
}

uint64_t last_drawn = 0;
char cursor_freq_string[16]={0};
char cursor_readout_string[16]={0};
void redrawCursor(){
  
  //don't draw too often
  if (last_drawn + 250 > millis())
    return; 
  last_drawn = millis();
  
  GLCD.FillRect(0, 0, 127, 11, WHITE);
  draw_safe(plot_cursor-2, 9, plot_cursor+2, 9);
  draw_safe(plot_cursor-1,10, plot_cursor+1, 10);  
  GLCD.SetDot(plot_cursor, 11,BLACK);

  //now the frequency and the value readout
  
  uint32_t mark_freq = ((f2-f1)* plot_cursor)/128l + f1;
  freqtoa(mark_freq, b);
  GLCD.DrawString(b, 60, 0);
  int val = plot_readings[plot_cursor];
  
  if (mode != MODE_ANTENNA_ANALYZER){
    itoa(val, b, 10);
    strcat(b, "dbm @");
  }else{

    //Serial.print(plot_cursor);Serial.print('=');Serial.print(val);
    int vswr_reading = 0;
    
    if (val < 30)
      vswr_reading = pgm_read_word_near(vswr + val);
    else
      vswr_reading = 10;
    //Serial.print(" ");Serial.println(vswr_reading);  
    int vswr_int = vswr_reading/10;
    int vswr_fraction = vswr_reading % 10;
    itoa(vswr_int, b, 10);
    strcat(b, ".");
    if (vswr_fraction < 10)
      strcat(b,"0");
    itoa(vswr_fraction, c, 10);
    strcat(b,c);
    
    strcat(b, " ");
    itoa(val, c, 10);
    strcat(b, c);
    strcat(b, "db");
  }
  GLCD.DrawString(b, 0, 0);
}

void updateCursor(){
  int e = enc_read(ENC_FAST);
  if (e == -1 && plot_cursor > 0){
    plot_cursor--;
    redrawCursor();
  }
  else if (e == 1 && plot_cursor < 127){
    plot_cursor++;
    redrawCursor();
  }
}

void setupSweep() {

  if (centerFreq + 500000l < spanFreq/2)
    centerFreq = spanFreq/2 + 500000l;

  //setup the limits
  f1 = centerFreq - spanFreq / 2;
  if (f1 < 500000l)
    f1 = 500000l;
  f2 = f1 + spanFreq;
  step_size = (f2 - f1) / 128;
  if (mode == MODE_MEASUREMENT_RX && step_size > 5000l)
    step_size = 5000l;

//  Serial.print("setup:"); Serial.print(f1); Serial.print(" to "); Serial.print(f2);
//  Serial.print(" stepsize1 "); Serial.println(step_size);

  current_freq = f1;
  si5351_reset();

  //switch the clocks on/off appropriately
  switch (mode) {
    case MODE_MEASUREMENT_RX:
      si5351a_clkoff(SI_CLK1_CONTROL);
      si5351a_clkoff(SI_CLK0_CONTROL);
      break;
    case MODE_NETWORK_ANALYZER:
      si5351a_clkoff(SI_CLK1_CONTROL);
      break;
    default:
      si5351a_clkoff(SI_CLK0_CONTROL);
  }
  
  tune_to(current_freq);
  //clear the screen
  GLCD.FillRect(0, 0, 127, 63, WHITE);
  si5351_reset();
  delay(50);
}

int xlat(int val) {

  if (mode == MODE_MEASUREMENT_RX || mode == MODE_NETWORK_ANALYZER) {
    //keep the readings between -20dbm and -100 dbm (80 db range)
    if (val > -10)
      val = -10;
    if (val < -110)
      val = -110;
    return -val / 2 + 5;
  }
  else {
    //return loss can be quite high
    if (val > 50)
      val = 50;
    return val + 13;
  }
}

static int prev_plot; //previous reading value, used to choose which value to keep in the plot_readings
int count_points = 0; //the number of measurements per sweep

void plot_point(uint32_t freq, int val) {
  static int16_t last_y = 0;
  static int16_t last_x = 0;

  int x = ((freq - f1) * 128l) / (f2 - f1);
  if (x < 0 || x > 127)
    return;
 
  if (mode == MODE_ANTENNA_ANALYZER) {
    if (x != prev_plot)
      plot_readings[x] = val;
    else if (val > plot_readings[x])
      plot_readings[x] = val;
  }
  else {
    if (x != prev_plot) {
      plot_readings[x] = val;
    }
    else if (val > plot_readings[x]) {
      plot_readings[x] = val;
    }
  }
  prev_plot = x;

  if (x != last_x) {
    // draw a vertical line, every 10 pixels, centered around 64
    if ((x -64) % 10 == 0){
      for (int i = 0; i < 50; i ++)
        if (i % 10)
          GLCD.SetDot(x, i+13, WHITE);
        else
          GLCD.SetDot(x, i+13, BLACK);
    }
    else  //clear the vertical line
      for (int i = 0; i < 50; i++)
        GLCD.SetDot(x, i+13, WHITE);
    last_x = x;
  }

  int y = xlat(val);
  //Serial.print("f");Serial.print(freq);Serial.print(":");Serial.println(y);
  if (x == 0)
    GLCD.SetDot(x, y, BLACK);
  else
    GLCD.DrawLine(x, last_y, x, y);
  last_y = y;
}

void doSweep() {

  if (current_freq >= f2) {
    current_freq = f1;
    count_points = 0;
    redrawCursor();
    //Serial.print("Step:"); Serial.println(step_size);
  }

  tune_to(current_freq);
  if(mode == MODE_ANTENNA_ANALYZER)
    delay(5);

  //Serial.print(current_freq/1000l);Serial.print("=");
  analogRead(DBM_READING);
  analogRead(DBM_READING);
  analogRead(DBM_READING);

  int r = analogRead(DBM_READING) / 5;
  if (mode == MODE_MEASUREMENT_RX || mode == MODE_NETWORK_ANALYZER) {
    plot_point(current_freq, r + dbmOffset);
  }
  else { // it is in vswr mode
    //Serial.print(openReading(current_freq));Serial.print(':');Serial.println(r);
    return_loss = openReading(current_freq) - r;
    if (return_loss > 50)
      return_loss = 50;
    if (return_loss < 0)
      return_loss = 0;
    //Serial.print("retloss:");Serial.println(return_loss);
    //int vswr_reading = pgm_read_word_near(vswr + return_loss);
    plot_point(current_freq, return_loss);
  }
  current_freq += step_size;
  count_points++;
}
