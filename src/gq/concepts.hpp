#pragma once
// Contratos (concepts) da biblioteca gq.
//
// Os NOMES abaixo são públicos: outros times já escrevem
//     template <gq::Numeric T> ...
// em seus próprios templates. As DEFINIÇÕES, porém, ficaram para depois e
// cada uma ainda aceita qualquer tipo. As regras que cada contrato deveria
// impor estão na seção "Problema" do README.

namespace gq {

// Tipo numérico sobre o qual faz sentido calcular estatísticas.
template <class T>
concept Numeric = true;  // provisório: aceita qualquer tipo

// Sequência percorrível cujos elementos são Numeric.
template <class R>
concept NumericRange = true;  // provisório: aceita qualquer tipo

// Tipo utilizável como chave de tabela hash.
template <class T>
concept Hashable = true;  // provisório: aceita qualquer tipo

// Tipo que gq::serialize sabe transformar em texto sem ambiguidade.
template <class T>
concept Serializable = true;  // provisório: aceita qualquer tipo

}  // namespace gq
