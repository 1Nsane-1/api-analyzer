#include <vector>
#include <string>

struct User { int id; std::string name; };
struct UserCreateRequest { std::string name; };

// @Controller UserController
// @Route /users

// @GET /users
// @Param page int
// @Param limit int
// @Return List<User>
std::vector<User> getUsers(int page, int limit) {
    return {};
}

// @POST /users
// @Body UserCreateRequest
// @Return User
User createUser(const UserCreateRequest& req) {
    return User{1, req.name};
}
