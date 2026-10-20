# Bibliografia

Livros usados como referência pelos desafios, organizados por trilha. Cada desafio cita
capítulos ou itens específicos na seção **Referências** do seu README. Os livros não são
distribuídos com o repositório.

## C++

| Livro | Autor(es) | Usado em |
|---|---|---|
| *Effective Modern C++* | Scott Meyers | C++11/14: smart pointers (Itens 18–22), funções membro especiais (17), lambdas (31–34) |
| *The C++ Programming Language*, 4ª ed. | Bjarne Stroustrup | referência geral C++11 |
| *C++ Memory Management* | Patrice Roy | RAII, ordem de destruição, `unique_ptr`/`shared_ptr` |
| *Modern C++ Programming Cookbook*, 3ª ed. | Marius Bancila | funções, utilitários, C++20 core features |
| *C++20 STL Cookbook* | Bill Weinman | concepts e constraints, views, `std::optional`/`variant` |
| *C++23 STL Cookbook* | Bill Weinman | recursos do C++23, `std::expected`, ranges, `std::print`, panorama do C++26 |
| *Template Metaprogramming with C++* | Marius Bancila | templates, SFINAE, concepts |
| *C++ Software Design* | Klaus Iglberger | design de APIs, value semantics |
| *C++ High Performance* | Björn Andrist, Viktor Sehr | custo de cópias, cache, medições |
| *Expert C++*, 2ª ed. | Marcelo Guerra Hahn et al. | concorrência, design |
| *C++ Crash Course* | Josh Lospinoso | fundamentos, testes |

## Estruturas de dados e algoritmos

| Livro | Autor(es) | Usado em |
|---|---|---|
| *Data Structures the Fun Way* | Jeremy Kubica | hash tables, árvores, heaps, tries, Bloom filters |
| *Data Structures and Algorithms with the C++ STL* | John Farrier | containers da STL, iteradores, algoritmos |
| Estrutura de Dados (material FIAP) | — | fundamentos |

## Go

| Livro | Autor(es) | Usado em |
|---|---|---|
| *A Linguagem de Programação Go* | Alan A. A. Donovan, Brian W. Kernighan | goroutines e canais (cap. 8), concorrência com variáveis compartilhadas (cap. 9), testes (cap. 11) |
| *Mastering Go*, 4ª ed. | Mihalis Tsoukalos | concorrência, `context`, profiling, generics |

## Docker, Kubernetes e sistemas distribuídos

| Livro | Autor(es) | Usado em |
|---|---|---|
| *Kubernetes: Up & Running*, 3ª ed. | Brendan Burns, Joe Beda, Kelsey Hightower, Lachlan Evenson | containers (cap. 2), Pods e health checks, Services, Deployments, ConfigMaps/Secrets |
| *The Complete Developer* | Martin Krause | Docker na prática |
| *Sistemas Distribuídos: Princípios e Paradigmas* | Andrew S. Tanenbaum, Maarten Van Steen | comunicação, consistência, tolerância a falhas |

## Arquitetura e engenharia

| Livro | Autor(es) |
|---|---|
| *Modern Software Engineering* | David Farley |
| *Software Architecture in Practice*, 4ª ed. | Len Bass, Paul Clements, Rick Kazman |
| *Domain-Driven Design Distilled* | Vaughn Vernon |
| *Balancing Coupling in Software Design* | Vlad Khononov |
| *Strategic Monoliths and Microservices* | Vaughn Vernon, Tomasz Jaskuła |
| *Patterns for API Design* | Olaf Zimmermann et al. |
| *Understanding Software Dynamics* | Richard L. Sites |

## Baixo nível

| Livro | Autor(es) |
|---|---|
| *Programação em Baixo Nível* | Igor Zhirkov |
| *C++ Programming for Linux Systems* | Desislav Andreev, Stanimir Lukanov |
| *Writing a C Compiler* | Nora Sandler |
| *Secure Coding in C/C++* (cheat sheet) | — |

Documentação oficial complementa os livros: cppreference.com, go.dev, docs.docker.com,
kubernetes.io e kind.sigs.k8s.io.
