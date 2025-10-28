/*
 * Генератор C кода из XML блок-схем
 * Компиляция: g++ codegen.cpp -o codegen -ltinyxml2
 * Запуск: ./codegen test.xml output.c
 */

#include <tinyxml2.h>

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace tinyxml2;
using namespace std;

struct Block {
  string type;
  string name;
  string sid;
  string gain;    // Для Gain блоков
  string inputs;  // Для Sum блоков знаки операций
};

struct Connection {
  string from;
  string to;
  int toPort;  // Номер входного порта
};

// Убираем пробелы из имени
string sanitizeName(const string& name) {
  string result = name;
  result.erase(remove(result.begin(), result.end(), ' '), result.end());
  return result;
}

// Парсинг XML:

Block parseBlock(XMLElement* elem) {
  Block b;

  b.type = elem->Attribute("BlockType") ? elem->Attribute("BlockType") : "";
  b.name = elem->Attribute("Name") ? elem->Attribute("Name") : "";
  b.sid = elem->Attribute("SID") ? elem->Attribute("SID") : "";

  // Убираем пробелы из имени
  b.name = sanitizeName(b.name);

  // Читаем параметры внутри блока
  for (XMLElement* p = elem->FirstChildElement("P"); p;
       p = p->NextSiblingElement("P")) {
    const char* name = p->Attribute("Name");
    const char* value = p->GetText();

    if (!name || !value) continue;

    if (string(name) == "Gain") b.gain = value;
    if (string(name) == "Inputs") b.inputs = value;
  }

  return b;
}

// Парсим строку вида "17#out:1", "17" - ID блока
string parseConnectionID(const string& str) {
  size_t pos = str.find('#');
  return (pos != string::npos) ? str.substr(0, pos) : "";
}

int parsePortNumber(const string& str) {
  size_t colonPos = str.find(':');
  if (colonPos != string::npos && colonPos + 1 < str.size()) {
    return stoi(str.substr(colonPos + 1));
  }
  return 1;
}

void parseConnections(XMLElement* line, vector<Connection>& connections) {
  string src, dst;

  // Читаем параметры (вход, выход)
  for (XMLElement* p = line->FirstChildElement("P"); p;
       p = p->NextSiblingElement("P")) {
    const char* name = p->Attribute("Name");
    const char* value = p->GetText();
    if (!name || !value) continue;

    if (string(name) == "Src") src = value;
    if (string(name) == "Dst") dst = value;
  }

  // Создаём связь
  if (!src.empty() && !dst.empty()) {
    connections.push_back(
        {parseConnectionID(src), parseConnectionID(dst), parsePortNumber(dst)});
  }

  // Обрабатываем ветвления (один выход идёт в несколько входов)
  for (XMLElement* branch = line->FirstChildElement("Branch"); branch;
       branch = branch->NextSiblingElement("Branch")) {
    for (XMLElement* p = branch->FirstChildElement("P"); p;
         p = p->NextSiblingElement("P")) {
      if (string(p->Attribute("Name") ? p->Attribute("Name") : "") == "Dst") {
        string branchDst = p->GetText();
        connections.push_back({parseConnectionID(src),
                               parseConnectionID(branchDst),
                               parsePortNumber(branchDst)});
      }
    }
  }
}

// Определение порядка выполнения блоков
vector<string> topologicalSort(const map<string, Block>& blocks,
                               const vector<Connection>& connections) {
  // Граф зависимостей
  map<string, set<string>> deps;
  set<string> allSIDs;

  // Инициализация
  for (const auto& pair : blocks) {
    if (pair.second.type != "Inport" && pair.second.type != "Outport") {
      deps[pair.first] = set<string>();
      allSIDs.insert(pair.first);
    }
  }

  // Строим граф зависимостей
  for (const auto& conn : connections) {
    if (blocks.count(conn.from) && blocks.at(conn.from).type == "Inport")
      continue;
    if (blocks.count(conn.to) && blocks.at(conn.to).type == "Outport") continue;

    // UnitDelay не создаёт зависимость (использует старое значение)
    if (blocks.count(conn.from) && blocks.at(conn.from).type == "UnitDelay")
      continue;

    if (!blocks.count(conn.from) || !blocks.count(conn.to)) continue;

    deps[conn.to].insert(conn.from);
  }

  // Алгоритм Кана
  vector<string> result;
  set<string> processed;

  while (result.size() < allSIDs.size()) {
    vector<string> ready;

    // Находим все блоки, готовые к обработке
    for (const auto& pair : deps) {
      if (processed.count(pair.first)) continue;

      bool allReady = true;
      for (const string& dep : pair.second) {
        if (!processed.count(dep)) {
          allReady = false;
          break;
        }
      }

      if (allReady) {
        ready.push_back(pair.first);
      }
    }

    if (ready.empty()) break;

    // Сортируем по SID
    sort(ready.begin(), ready.end(),
         [](const string& a, const string& b) { return stoi(a) < stoi(b); });

    result.push_back(ready[0]);
    processed.insert(ready[0]);
  }

  return result;
}

// Для генерации кода:

// Находим что идёт к конкретному входному порту блока
string findInput(const string& blockSID, int portNum,
                 const map<string, Block>& blocks,
                 const vector<Connection>& connections) {
  for (const auto& conn : connections) {
    if (conn.to == blockSID && conn.toPort == portNum) {
      return "nwocg." + blocks.at(conn.from).name;
    }
  }
  return "0";
}

// Находим все входы блока с сохранением порядка портов
vector<string> findAllInputs(const string& blockSID,
                             const map<string, Block>& blocks,
                             const vector<Connection>& connections) {
  map<int, string> portMap;  // номер порта, имя переменной

  for (const auto& conn : connections) {
    if (conn.to == blockSID) {
      portMap[conn.toPort] = "nwocg." + blocks.at(conn.from).name;
    }
  }

  // Преобразуем map в vector, сохраняя порядок портов
  vector<string> result;
  for (const auto& pair : portMap) {
    result.push_back(pair.second);
  }

  return result;
}

// Генерируем код для одного блока
string generateBlockCode(const Block& block, const map<string, Block>& blocks,
                         const vector<Connection>& connections) {
  if (block.type == "Sum") {
    vector<string> inputs = findAllInputs(block.sid, blocks, connections);
    string signs =
        block.inputs.empty() ? string(inputs.size(), '+') : block.inputs;

    string code = "    nwocg." + block.name + " = ";
    for (size_t i = 0; i < inputs.size(); i++) {
      if (i > 0) {
        code += (signs[i] == '+' ? " + " : " - ");
      } else if (signs[i] == '-') {
        code += "-";
      }
      code += inputs[i];
    }
    return code + ";\n";
  } else if (block.type == "Gain") {
    string input = findInput(block.sid, 1, blocks, connections);
    return "    nwocg." + block.name + " = " + input + " * " + block.gain +
           ";\n";
  } else if (block.type == "UnitDelay") {
    // UnitDelay уже содержит нужное значение (из прошлого шага)
    return "";
  }

  return "";
}

void generateCode(const map<string, Block>& blocks,
                  const vector<Connection>& connections,
                  const vector<string>& order, const string& outputFile) {
  // Открываем файл в режиме перезаписи
  ofstream out(outputFile, ios::out | ios::trunc);

  // Собираем списки входов и выходов
  vector<pair<int, string>> inportsWithSID;  // SID, имя
  vector<string> outports;
  map<string, string> outportSources;  // имя outport, имя переменной источника

  for (const auto& pair : blocks) {
    if (pair.second.type == "Inport") {
      inportsWithSID.push_back({stoi(pair.first), pair.second.name});
    } else if (pair.second.type == "Outport") {
      outports.push_back(pair.second.name);
      // Находим что подключено к этому выходу
      for (const auto& conn : connections) {
        if (conn.to == pair.first) {
          outportSources[pair.second.name] = blocks.at(conn.from).name;
        }
      }
    }
  }

  // Сортируем входы по SID (порядок объявления в XML)
  sort(inportsWithSID.begin(), inportsWithSID.end());
  vector<string> inports;
  for (const auto& p : inportsWithSID) {
    inports.push_back(p.second);
  }

  // Запись кода в файл
  out << "#include \"nwocg_run.h\"\n";
  out << "#include <math.h>\n\n";

  out << "static struct\n{\n";

  // Входные порты в порядке их объявления
  for (const string& name : inports) {
    out << "    double " << name << ";\n";
  }

  // Остальные блоки в порядке выполнения
  for (const string& sid : order) {
    const Block& block = blocks.at(sid);
    out << "    double " << block.name << ";\n";
  }

  out << "} nwocg;\n\n";

  out << "void nwocg_generated_init()\n{\n";

  for (const string& sid : order) {
    const Block& block = blocks.at(sid);
    if (block.type == "UnitDelay") {
      out << "    nwocg." << block.name << " = 0;\n";
    }
  }
  out << "}\n\n";

  out << "void nwocg_generated_step()\n{\n";

  // Генерируем код для каждого блока в правильном порядке
  for (const string& sid : order) {
    const Block& block = blocks.at(sid);
    string code = generateBlockCode(block, blocks, connections);
    if (!code.empty()) out << code;
  }

  for (const string& sid : order) {
    const Block& block = blocks.at(sid);
    if (block.type == "UnitDelay") {
      string input = findInput(sid, 1, blocks, connections);
      out << "    nwocg." << block.name << " = " << input << ";\n";
    }
  }

  out << "}\n\n";

  out << "static const nwocg_ExtPort\n";
  out << "ext_ports[] =\n{\n";

  // Выходные порты (is_input = 0)
  for (const string& name : outports) {
    string source = outportSources[name];
    out << "    { \"" << name << "\", &nwocg." << source << ", 0 },\n";
  }

  // Входные порты (is_input = 1) - в обратном порядке SID
  for (auto it = inports.rbegin(); it != inports.rend(); ++it) {
    out << "    { \"" << *it << "\", &nwocg." << *it << ", 1 },\n";
  }

  out << "    { 0, 0, 0 },\n";
  out << "};\n\n";

  out << "const nwocg_ExtPort * const\n";
  out << "nwocg_generated_ext_ports = ext_ports;\n\n";

  out << "const size_t\n";
  out << "nwocg_generated_ext_ports_size = sizeof(ext_ports);\n";

  out.close();

  cout << "Код сгенерирован: " << outputFile << endl;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    cout << "Использование: " << argv[0] << " input.xml output.c" << endl;
    return 1;
  }

  XMLDocument doc;
  if (doc.LoadFile(argv[1]) != XML_SUCCESS) {
    cerr << "Ошибка чтения XML файла" << endl;
    return 1;
  }

  XMLElement* root = doc.FirstChildElement("System");
  if (!root) {
    cerr << "Нет элемента <System>" << endl;
    return 1;
  }

  // Парсим блоки
  map<string, Block> blocks;
  for (XMLElement* elem = root->FirstChildElement("Block"); elem;
       elem = elem->NextSiblingElement("Block")) {
    Block b = parseBlock(elem);
    blocks[b.sid] = b;
  }

  // Парсим связи
  vector<Connection> connections;
  for (XMLElement* elem = root->FirstChildElement("Line"); elem;
       elem = elem->NextSiblingElement("Line")) {
    parseConnections(elem, connections);
  }

  vector<string> order = topologicalSort(blocks, connections);

  cout << "Генерация кода..." << endl;
  string outputFile = argv[2];
  generateCode(blocks, connections, order, outputFile);

  cout << "Готово!" << endl;
  return 0;
}