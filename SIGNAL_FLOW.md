# Схема передачи сигнала

```mermaid
%%{init: {'theme':'base','themeVariables': {'fontSize':'16px'}, 'flowchart': {'curve':'basis','nodeSpacing': 18,'rankSpacing': 20,'padding': 8,'useMaxWidth': false}}}%%
flowchart TB
    A[Команда] --> B[Node.js сервер]
    B --> C[COM-порт]
    C --> D[Arduino]
    D --> E[Датчики]
    E --> F[Сбор данных]
    F --> D
    D --> G[Ответ назад]
    G --> B
```

## Кратко

Команда поступает на сервер, затем передаётся через COM-порт на Arduino. Arduino отправляет сигнал датчикам, получает данные и возвращает ответ обратно по той же цепочке.
