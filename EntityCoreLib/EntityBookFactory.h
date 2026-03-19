#pragma once
#include <cstddef>
#include <expected>
#include <memory>

struct EntityBookError;   // forward declare
class EntityBook;         // forward declare

std::expected<std::unique_ptr<EntityBook>, EntityBookError>
CreateEntityBook(std::size_t capacity);