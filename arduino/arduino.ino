#define SIZE 6
#define DIVIDER 2
#define MAXMAX 10000
#define MINMIN -10000

struct sensor
{
  int pin;          // Numer pinu, na którym jest podpięty czujnik
  int read_val;     // Ostatnio odczytana wartość
  int sum;          // Suma ostatnich wartości
  int max_val;      // Maksymalna wartość wśród ostatnich
  int min_val;      // Minimalna wartość wśród ostatnich
  int avg_val;      // Średnia wartość z ostatnich pomiarów
  int buffer[SIZE]; // Tabela przechowująca ostatnie wartości odczytów
  bool flag;        // Informacja, czy świeci się lampka {1 - nie, 0 - tak}
  int above;        // Zmienne do określania ile ostatnich
  int below;        // pomiarów znajdowało się nad lub pod progiem
  int led;          // Pin obsługujący diodę
  String code;      // Litera określająca nazwę lampki
};

sensor sensors[] = // Tablica przechowująca dane o czujnikach i lampkach
{
  {A0, 0, 0, MINMIN, MAXMAX, 0, {0}, false, 0, 5, 4, "B"}, // Niebieski
  {A1, 0, 0, MINMIN, MAXMAX, 0, {0}, false, 0, 5, 5, "R"}, // Czerwony
  {A2, 0, 0, MINMIN, MAXMAX, 0, {0}, false, 0, 5, 6, "G"}, // Zielony
  {A3, 0, 0, MINMIN, MAXMAX, 0, {0}, false, 0, 5, 7, "Y"}  // Żółty
};

int head = 0;                  // Zmienna do iterowania po tabeli ostatnich wartości
int light_threshold = 950;     // Próg do ustalania, czy świeci się dana lampka
int global_state = 0;          // Informacja, które lampki się świecą
int previous_global_state = 0; // Informacja o stanie, który był wcześniej
int last_state = 0;            // Informacja o ostatnim stanie tymczasowym
int states_count = 5;          // Określenie ile razy wystąpił ostatni stan
String result = "";            // Wynik działania programu
unsigned long last_change = 0; // Czas ostatniej zmiany stanu świecenia
bool start_stop = 0;           // Przełącznik działania układu {0 - wyłączony, 1 - włączony}
int button_start_stop = 2;     // Pin odpowiadający za przycisk wyłączajacy układ
unsigned long last_break = 0;  // Czas który upłynął od ostatniego przerwania
int control_led = 8;           // Pin na którym znajduje się dioda kontrolna
int button_report_error = 3;   // Przycisk do zgłaszania błędów
unsigned long last_error = 0;  // Czas który upłynął od ostatniego błędu

int updatemax(sensor &sen) // Funkcja wyznaczająca wartość maksymalną
{
  int max = MINMIN;
  for(int i = 0; i < SIZE; i++) if(sen.buffer[i] > max) max = sen.buffer[i];
  return max;
}

int updatemin(sensor &sen) // Funkcja wyznaczająca wartość minimalną
{
  int min = MAXMAX;
  for(int i = 0; i < SIZE; i++) if(sen.buffer[i] < min) min = sen.buffer[i];
  return min;
}

void swap(int &a, int &b) // Funkcja zamieniająca dwie wartości
{
  int temp = a;
  a = b;
  b = temp;
}

void updatesensor(sensor &sen) // Funkcja odczytująca nową wartość i aktualizujaca dane
{
  sen.read_val = analogRead(sen.pin);   // Odczyt nowych danych
  sen.sum += sen.read_val;              // Aktualizacja sumy poprzez dodanie nowej
  sen.sum -= sen.buffer[head];          // i usunięcie najstarszej wartości
  swap(sen.read_val, sen.buffer[head]); // Wpisanie nowej wartości w miejsce starej
  if(sen.buffer[head] > sen.max_val)    // Jeżeli nowa wartość jest większa od maksymalnej,
  {sen.max_val = sen.buffer[head];}     // to zapisuje się ją jako maksymalną
  else if(sen.read_val == sen.max_val)  // Jeśli usuwana wartość była maksymalna,
  {sen.max_val = updatemax(sen);}       // należy znaleźć w całej tablicy nową.
  if(sen.buffer[head] < sen.min_val)    // Analogicznie dla wartości minimalnej
  {sen.min_val = sen.buffer[head];}
  else if(sen.read_val == sen.min_val)
  {sen.min_val = updatemin(sen);}
  sen.avg_val = sen.sum - sen.max_val - sen.min_val; // Wartość średnia obliczana
  sen.avg_val = sen.avg_val >> DIVIDER;              // przesunięciem bitowym
}

void updateflag(sensor &sen) // Funkcja określająca, w jakim stanie znajduje się lampka
{
  if(sen.avg_val >= light_threshold) // Jeżeli najnowsza średnia wartość
  {                                  // znajduje się powyżej progu
    sen.above++;                     // funkcja powiększa licznik tego stanu
    sen.below = 0;                   // i resetuje licznik stanu przeciwnego
  }
  if(sen.avg_val < light_threshold)  // Analogicznie dla sytuacji odwrotnej
  {
    sen.below++;
    sen.above = 0;
  }
  if(sen.above >= 5)                 // Jeżeli licznik stanu 'powyżej progu'
  {                                  // osiągnie wartość 5
    sen.flag = 1;                    // ustawia się wartość flagi
    sen.above = 5;
  }
  if(sen.below >= 5)                 // Analogicznie gdy stan 'poniżej progu'
  {                                  // utrzyma się przez ostatnich minimum 5 iteracji
    sen.flag = 0;
    sen.below = 5;
  }
}

int calculatestate(bool f0, bool f1, bool f2, bool f3) // Funkcja oblicza stan układu na podstawie wartości flag
{
  return (int(f3) << 3) | (int(f2) << 2) | (int(f1) << 1) | int(f0);
}

void updateglobal() // Funkcja ustala stan całego układu lampek
{
  previous_global_state = global_state; // Zapisanie stanu sprzed sktualizacji
  int new_state = calculatestate(sensors[0].flag, sensors[1].flag, sensors[2].flag, sensors[3].flag); // Obliczenie tymczasowego stanu bieżącej iteracji
  if(last_state == new_state) states_count++; // Jeżeli bieżący tymczasowy stan jest taki jak poprzedni, zwiększ licznik wystąpienia tego stanu
  else                                        // W przeciwnym razie zresetuj licznik i zapisz nowy tymczasowy stan
  {
    last_state = new_state;
    states_count = 0;
  }
  if(states_count >= 5)       // Jeżeli stan utrzymuje się przez minimum 5 ostatnich iteracji
  {
    global_state = new_state; // zaktualizuj główny stan
    states_count = 5;
  }
}

void handlestatechange() // Funkcja obsługująca moment zmiany stanu
{
  int changed_sensor; // Zmienna informująca na jakiej lampce nastąpiła zmiana
  if(global_state == 1 || global_state == 14) changed_sensor = 0; // Przypisanie sensora do zmiennej
  else if(global_state == 2 || global_state == 13) changed_sensor = 1;
  else if(global_state == 4 || global_state == 11) changed_sensor = 2;
  else if(global_state == 8 || global_state == 7) changed_sensor = 3;
  else changed_sensor = -1; // Pomiń, jeżeli nie wykryto
  if(changed_sensor >= 0)   // żadnego z tych stanów
  {
    if(previous_global_state == 0) // Sytuacja występująca w czasie nadawania sekwencji przez grę
    {
      result = result + sensors[changed_sensor].code;   // Dopisz właściwą literę do wyniku
      digitalWrite(sensors[changed_sensor].led, HIGH); // Zapal diodę na krótki czas
      delay(50);
      digitalWrite(sensors[changed_sensor].led, LOW);
    }
    if(previous_global_state == 15) // Sytuacja występująca w czasie wprowadzania sekwencji przez gracza
    {
      Serial.println("usr: " + sensors[changed_sensor].code); // Napisz którą lamkę nacisnął gracz
      digitalWrite(sensors[changed_sensor].led, HIGH); // Zapal diodę na krótki czas
      delay(50);
      digitalWrite(sensors[changed_sensor].led, LOW);
    }
    last_change = millis(); // Zapisz czas ostatniej zmiany stanu
  }
}

void change_start_stop() // Funkcja obsługująca przerwanie pracy
{
  if(millis() - last_break > 1000) // Jeżeli od ostatniej zmiany minęła sekunda
  {
    if(start_stop) digitalWrite(control_led, LOW); // W przypadku wyłączania zgaś diodę
    else digitalWrite(control_led, HIGH); // W przypadku włączania zapal diodę
    start_stop = !start_stop; // Zmień stan
    last_break = millis(); // Zapisz czas zmiany
  }
}

void report_error() // Funkcja obsługująca zgłoszenie błędu
{
  if(millis() - last_error > 1000 && start_stop) // Jeżeli od ostatniego zgłoszenia błędu minęła sekunda
  {
    Serial.println("usr: error"); // Nadaj komunikat o błędzie
    last_error = millis(); // Zapisz czas zmiany ostatniego błędu
  }
}

void setup()
{
  Serial.begin(9600);
  pinMode(control_led, OUTPUT); // Ustawienie trybu obsługi pinu diody kontrolnej
  digitalWrite(control_led, LOW); // Na początku program jest wyłączony
  pinMode(button_start_stop, INPUT_PULLUP); // Obsługa przycisku przerwania programu
  attachInterrupt(digitalPinToInterrupt(button_start_stop), change_start_stop, FALLING); // Ustawienie przerwania na przycisku
  pinMode(button_report_error, INPUT_PULLUP); // Obsługa przycisku zgłaszania błedu
  attachInterrupt(digitalPinToInterrupt(button_report_error), report_error, FALLING); // Ustawienie przerwania na przycisku
  for(int i = 0; i < 4; i++) // Pobranie początkowych danych dla wszystkich 4 czujników
  {
    pinMode(sensors[i].pin, INPUT);    // Ustawienie trybu obsługi pinów
    pinMode(sensors[i].led, OUTPUT);   // Ustawienie trybu obsługi pinów
    digitalWrite(sensors[i].led, LOW); // Wyłączenie wszystkich diód
    for(int j = 0; j < SIZE; j++)
    {
      sensors[i].buffer[j] = analogRead(sensors[i].pin); // Odczyt danych
      sensors[i].sum += sensors[i].buffer[j]; // Aktualizacja sum
    }
    sensors[i].max_val = updatemax(sensors[i]); // Obliczenie wartości
    sensors[i].min_val = updatemin(sensors[i]); // maksmymalnej i minimalnej
    sensors[i].avg_val = sensors[i].sum - sensors[i].max_val - sensors[i].min_val; // Obliczenie początkowej średniej wartości
    sensors[i].avg_val = sensors[i].avg_val >> DIVIDER;
  }
}

void loop()
{
  if(start_stop) // Program działa tylko, gdy ustawiona jest flaga
  {
    for(int i = 0; i < 4; i++) updateflag(sensors[i]); // Aktualizacja stanu świecenia każdej lampki
    updateglobal(); // Aktualizacja stanu całej gry
    if(previous_global_state != global_state) handlestatechange(); // W momencie zmiany stanu wykonaj działania
    if(millis() - last_change > 2000 && result != "") // Jeżeli od ostatniej zmiany stanu minęły dwie sekundy
    {                                                 // i sekwencja nie jest pusta
      Serial.println("seq: " + result);               // wypisz wynik
      result = "";                                    // i zresetuj sekwencję
    }
    for(int i = 0; i < 4; i++) updatesensor(sensors[i]); // Zaktualizuj odczyty
    head = (head + 1) % SIZE; // Przesuń indeks najstarszego odczytu
    delay(10); // Opóźnienie kolejnej iteracji
  }
}