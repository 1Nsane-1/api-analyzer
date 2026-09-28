# Документация API

## Контроллер: UserController
**Базовый маршрут:** `/users`

### GET /users
**Параметры:**
- `page` (int)
- `limit` (int)
**Возвращает:** `List<User>`

### POST /users
**Тело запроса:** `UserCreateRequest`
**Возвращает:** `User`

## Модели данных (DTO / ViewModel)
- `List<User>`
- `User`
- `UserCreateRequest`
