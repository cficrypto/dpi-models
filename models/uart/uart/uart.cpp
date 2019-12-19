/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#include "dpi/models.hpp"
#include <stdint.h>
#include <unistd.h>
#include <thread>

class Uart_tb;

class Uart_tb_uart_itf : public Uart_itf
{
public:
  Uart_tb_uart_itf(Uart_tb *top) : top(top) {}
  void tx_edge(int64_t timestamp, int data);

private:
    Uart_tb *top;
};

class Uart_tb : public Dpi_model
{
public:
  Uart_tb(js::config *config, void *handle);

  void tx_edge(int64_t timestamp, int tx);
  void rx_edge(int64_t timestamp, int rx);
  
  void tx_sampling();
  void rx_sampling();
  
  void start();

private:

  void dpi_task(void);
  bool rx_is_sampling(void);
  void stdin_task(void);
  static void dpi_uart_task_stub(Uart_tb *);
  static void dpi_uart_stdin_task_stub(Uart_tb *);

  void start_tx_sampling(int baudrate);
  void stop_tx_sampling();
  
  void start_rx_sampling(int baudrate);
  void stop_rx_sampling();

  int period;
  bool tx_wait_start = true;
  bool tx_wait_stop = false;
  int current_tx;
  int current_rx;
  int baudrate;
  int nb_bits;
  uint32_t rx_bit_buffer = 0;
  int rx_nb_bits = 0;
  bool loopback;
  bool stdout;
  bool stdin = true;
  bool sampling_rx = false;
  bool sampling_tx = false;
  uint8_t byte;
  FILE *tx_file = NULL;

  std::thread *stdin_thread;
  pthread_mutex_t rx_mutex;
  Uart_itf *uart;
};

void Uart_tb::start()
{
  create_task((void *)&Uart_tb::dpi_uart_task_stub, this);
}

void Uart_tb::dpi_uart_task_stub(Uart_tb *_this)
{
  _this->dpi_task();
}

void Uart_tb::dpi_uart_stdin_task_stub(Uart_tb *_this)
{
  _this->stdin_task();
}

Uart_tb::Uart_tb(js::config *config, void *handle) : Dpi_model(config, handle)
{
  baudrate = config->get("baudrate")->get_int();
  loopback = config->get("loopback")->get_bool();
  stdout = config->get("stdout")->get_bool();
  stdin = config->get("stdin")->get_bool();
  std::string tx_filename = config->get("tx_file")->get_str();
  pthread_mutex_init(&rx_mutex, NULL);
  period = 1000000000000/baudrate;
  print("Instantiated uart model (baudrate: %d, loopback: %d, stdout: %d, tx_file: %s)", baudrate, loopback, stdout, tx_filename.c_str());
  if (tx_filename != "")
  {
    tx_file = fopen(tx_filename.c_str(), (char *)"w");
    if (tx_file == NULL)
    {
      print("Unable to open TX log file: %s", strerror(errno));
    }
  }
  uart = new Uart_tb_uart_itf(this);
  create_itf("uart", static_cast<Uart_itf *>(uart));
  if(stdin)
  {
    stdin_thread = new std::thread(&Uart_tb::stdin_task, this);
  }
}

void Uart_tb::tx_sampling()
{
  print("Sampling bit (value: %d)", current_tx);

  if (tx_wait_stop)
  {
    if (current_tx == 1)
    {
      print("Received stop bit");
      tx_wait_start = true;
      tx_wait_stop = false;
      this->stop_tx_sampling();
    }
  }
  else
  {
    print("Received data bit (data: %d)", current_tx);
    byte = (byte >> 1) | (current_tx << 7);
    nb_bits++;
    if (nb_bits == 8) {
      print("Sampled TX byte (value: 0x%x)", byte);
      if (stdout) printf("%c", byte);
      if (tx_file) {
        fwrite((void *)&byte, 1, 1, tx_file);
      }
      print("Waiting for stop bit");
      tx_wait_stop = true;
    }
  }
}

void Uart_tb::rx_sampling()
{
  pthread_mutex_lock(&rx_mutex);
  this->current_rx = this->rx_bit_buffer & 0x1;
  this->rx_bit_buffer = this->rx_bit_buffer >> 1;
  printf("Sampling bit (value: %d)\n", current_rx);

  uart->rx_edge(this->current_rx);
  rx_nb_bits++;
  if(rx_nb_bits == 10)
  {
    this->stop_rx_sampling();
  }
  pthread_mutex_unlock(&rx_mutex);
}

void Uart_tb::tx_edge(int64_t timestamp, int tx)
{
  if (loopback) uart->rx_edge(tx);

  current_tx = tx;
  
  if (tx_wait_start && tx == 0)
  {
    print("Received start bit");
    this->start_tx_sampling(baudrate);
    tx_wait_start = false;
    nb_bits = 0;
  }
}

void Uart_tb::start_tx_sampling(int baudrate)
{
  this->sampling_tx = 1;
  this->raise_event();
}

void Uart_tb::stop_tx_sampling(void)
{
  this->sampling_tx = 0;
}

// will be called protected by a mutex
void Uart_tb::start_rx_sampling(int baudrate)
{
  this->sampling_rx = 1;
}

void Uart_tb::stop_rx_sampling(void)
{
    this->sampling_rx = 0;
    rx_nb_bits = 0;
}

void Uart_tb::dpi_task(void)
{
  while(1)
  {
    while(!(this->rx_is_sampling() || this->sampling_tx))
    { 
      this->wait_event();
    }

    wait_ps(period/2);

    while(this->rx_is_sampling() || this->sampling_tx)
    {
      this->wait_ps(period);
      if(this->sampling_tx)
      {
        this->tx_sampling();
      }
      if(this->rx_is_sampling())
      {
        this->rx_sampling();
      }
    }
  }
}


bool Uart_tb::rx_is_sampling(void)
{
    pthread_mutex_lock(&rx_mutex);
    bool ret = this->sampling_rx;
    pthread_mutex_unlock(&rx_mutex);
    return ret;
}

void Uart_tb::stdin_task(void)
{
  while(1)
  {
    printf("stdin task sampling\n");
    int c = 0;
    c = getchar();
    printf("got char:%c\n",c);
    while(this->rx_is_sampling())
    {// TODO: use cond instead
      usleep(5);
    }
    pthread_mutex_lock(&rx_mutex);
    rx_bit_buffer = 0;
    rx_bit_buffer |= c << 1;
    rx_bit_buffer |= 1 << 9;
    rx_nb_bits = 0;
    this->start_rx_sampling(baudrate);
    pthread_mutex_unlock(&rx_mutex);
    printf("raising event\n");
    raise_event_from_ext();
    printf("raised_event\n");
  }
}

void Uart_tb_uart_itf::tx_edge(int64_t timestamp, int data)
{
  top->tx_edge(timestamp, data);
}

extern "C" Dpi_model *dpi_model_new(js::config *config, void *handle)
{
  return new Uart_tb(config, handle);
}
