// DIAGNÓSTICO: mean(vector<string>) falha e a mensagem do compilador cita o concept violado
// Este arquivo NÃO deve compilar. O avaliador confere que o erro é reportado
// no ponto de chamada citando o contrato (Numeric/NumericRange), e não como
// um erro obscuro dentro do corpo do template.
#include <string>
#include <vector>

#include "gq/metrics.hpp"

int main() {
  const std::vector<std::string> names{"a", "b"};
  return static_cast<int>(gq::mean(names));
}
