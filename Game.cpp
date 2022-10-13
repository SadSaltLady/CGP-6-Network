#include "Game.hpp"

#include "Connection.hpp"

#include <stdexcept>
#include <iostream>
#include <cstring>

#include <glm/gtx/norm.hpp>

void Player::Controls::send_controls_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 5;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));

	auto send_button = [&](Button const &b) {
		if (b.downs & 0x80) {
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t( (b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f) ) );
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
	send_button(jump);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	//expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	if (size != 5) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 5!");
	
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	auto recv_button = [](uint8_t byte, Button *button) {
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255) {
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[4+0], &left);
	recv_button(recv_buffer[4+1], &right);
	recv_button(recv_buffer[4+2], &up);
	recv_button(recv_buffer[4+3], &down);
	recv_button(recv_buffer[4+4], &jump);

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}


//-----------------------------------------

Game::Game() : mt(0x15466666) {
}

Player *Game::spawn_player() {
	players.emplace_back();
	Player &player = players.back();

	//random point in the middle area of the arena:
	player.position.x = glm::mix(ArenaMin.x + 2.0f * PlayerRadius, ArenaMax.x - 2.0f * PlayerRadius, 0.4f + 0.2f * mt() / float(mt.max()));
	player.position.y = glm::mix(ArenaMin.y + 2.0f * PlayerRadius, ArenaMax.y - 2.0f * PlayerRadius, 0.4f + 0.2f * mt() / float(mt.max()));

	do {
		player.color.r = mt() / float(mt.max());
		player.color.g = mt() / float(mt.max());
		player.color.b = mt() / float(mt.max());
	} while (player.color == glm::vec3(0.0f));
	player.color = glm::normalize(player.color);

	player.name = "Player " + std::to_string(next_player_number++);

	return &player;
}

void Game::remove_player(Player *player) {
	bool found = false;
	for (auto pi = players.begin(); pi != players.end(); ++pi) {
		if (&*pi == player) {
			players.erase(pi);
			found = true;
			break;
		}
	}
	assert(found);
}

void Game::remove_enemy(Enemy *enemy) {
	bool found = false;
	for (auto ei = enemies.begin(); ei != enemies.end(); ++ei) {
		if (&*ei == enemy) {
			enemies.erase(ei);
			found = true;
			enemy_killed++;
			break;
		}
	}
	assert(found);
}

Enemy *Game::spawn_enemy() {
	enemies.emplace_back();
	Enemy &enemy = enemies.back();	

	//random point on the edge of the arena:
	float x = ArenaMin.x + mt() / float(mt.max()) * (ArenaMax.x - ArenaMin.x);
	float y = ArenaMin.y + mt() / float(mt.max()) * (ArenaMax.y - ArenaMin.y);
	if (mt() % 2) {
		enemy.position.x = (mt() % 2) ? ArenaMin.x : ArenaMax.x;
		enemy.position.y = y;
	} else {
		enemy.position.x = x;
		enemy.position.y = (mt() % 2) ? ArenaMin.y : ArenaMax.y;
	}
	//random point in the middle area of the arena:
	//enemy.position.x = glm::mix(ArenaMin.x + 2.0f * EnemyRadius, ArenaMax.x - 2.0f * EnemyRadius, 0.4f + 0.2f * mt() / float(mt.max()));
	//enemy.position.y = glm::mix(ArenaMin.y + 2.0f * EnemyRadius, ArenaMax.y - 2.0f * EnemyRadius, 0.4f + 0.2f * mt() / float(mt.max()));

	enemy.color.r = mt() / float(mt.max());
	enemy.color.g = mt() / float(mt.max());
	enemy.color.b = mt() / float(mt.max());
	enemy.color = glm::normalize(enemy.color);
	return &enemy;
}

void Game::update(float elapsed) {

	time += elapsed;
	//see if the two players are connected -- use jump as the "connect" button:
	if (players.size() == 2) {
		bool p1_con = players.front().controls.jump.pressed;
		bool p2_con = players.back().controls.jump.pressed;
		connected = p1_con && p2_con;
	} else {
		connected = false;
	}

	//position/velocity update:
	for (auto &p : players) {
		glm::vec2 dir = glm::vec2(0.0f, 0.0f);
		if (p.controls.left.pressed) dir.x -= 1.0f;
		if (p.controls.right.pressed) dir.x += 1.0f;
		if (p.controls.down.pressed) dir.y -= 1.0f;
		if (p.controls.up.pressed) dir.y += 1.0f;

		if (dir == glm::vec2(0.0f)) {
			//no inputs: just drift to a stop
			float amt = 1.0f - std::pow(0.5f, elapsed / (PlayerAccelHalflife * 2.0f));
			p.velocity = glm::mix(p.velocity, glm::vec2(0.0f,0.0f), amt);
		} else {
			//inputs: tween velocity to target direction
			dir = glm::normalize(dir);

			float amt = 1.0f - std::pow(0.5f, elapsed / PlayerAccelHalflife);

			//accelerate along velocity (if not fast enough):
			float along = glm::dot(p.velocity, dir);
			if (along < PlayerSpeed) {
				along = glm::mix(along, PlayerSpeed, amt);
			}

			//damp perpendicular velocity:
			float perp = glm::dot(p.velocity, glm::vec2(-dir.y, dir.x));
			perp = glm::mix(perp, 0.0f, amt);

			p.velocity = dir * along + glm::vec2(-dir.y, dir.x) * perp;
		}
		//if connected, slow player down:
		if (connected) p.velocity *= 0.5f;
		p.position += p.velocity * elapsed;

		//reset 'downs' since controls have been handled:
		p.controls.left.downs = 0;
		p.controls.right.downs = 0;
		p.controls.up.downs = 0;
		p.controls.down.downs = 0;
		p.controls.jump.downs = 0;
	}

	//collision resolution:
	for (auto &p1 : players) {
		//player/player collisions:
		for (auto &p2 : players) {
			if (&p1 == &p2) break;
			glm::vec2 p12 = p2.position - p1.position;
			float len2 = glm::length2(p12);
			if (len2 > (2.0f * PlayerRadius) * (2.0f * PlayerRadius)) continue;
			if (len2 == 0.0f) continue;
			glm::vec2 dir = p12 / std::sqrt(len2);
			//mirror velocity to be in separating direction:
			glm::vec2 v12 = p2.velocity - p1.velocity;
			glm::vec2 delta_v12 = dir * glm::max(0.0f, -1.75f * glm::dot(dir, v12));
			p2.velocity += 0.5f * delta_v12;
			p1.velocity -= 0.5f * delta_v12;
		}
		//player/arena collisions:
		if (p1.position.x < ArenaMin.x + PlayerRadius) {
			p1.position.x = ArenaMin.x + PlayerRadius;
			p1.velocity.x = std::abs(p1.velocity.x);
		}
		if (p1.position.x > ArenaMax.x - PlayerRadius) {
			p1.position.x = ArenaMax.x - PlayerRadius;
			p1.velocity.x =-std::abs(p1.velocity.x);
		}
		if (p1.position.y < ArenaMin.y + PlayerRadius) {
			p1.position.y = ArenaMin.y + PlayerRadius;
			p1.velocity.y = std::abs(p1.velocity.y);
		}
		if (p1.position.y > ArenaMax.y - PlayerRadius) {
			p1.position.y = ArenaMax.y - PlayerRadius;
			p1.velocity.y =-std::abs(p1.velocity.y);
		}
	}
	
	//enemy/player collisions:
	for (auto &e : enemies) {
		for (auto &p : players) {
			glm::vec2 ep = e.position - p.position;
			float len2 = glm::length2(ep);
			if (len2 > (PlayerRadius + EnemyRadius) * (PlayerRadius + EnemyRadius)) continue;
			if (len2 == 0.0f) continue;
			glm::vec2 dir = ep / std::sqrt(len2);
			//mirror velocity to be in separating direction:
			glm::vec2 vep = e.velocity - p.velocity;
			glm::vec2 delta_vep = dir * glm::max(0.0f, -1.75f * glm::dot(dir, vep));
			e.velocity += 0.5f * delta_vep;
			p.velocity -= 0.5f * delta_vep;
		}
	}
	
	//enemy position/velocity update:
	for (auto &e : enemies) {
		//enemy/arena collisions:
		//get a direction from position to the center of the arena:
		glm::vec2 dir = glm::normalize(glm::vec2(0.f, 0.f) - e.position);
		float amt = 1.0f - std::pow(0.5f, elapsed / (EnemyAccelHalflife * 2.0f));
		e.velocity += dir * EnemyAccel * elapsed;
		e.velocity = glm::mix(e.velocity, glm::vec2(0.0f,0.0f), amt);

		//make sure enemy velocity to the center is not too fast:
		float speed = glm::dot(e.velocity, dir);
		if (speed > EnemySpeed) {
			speed = glm::mix(speed, EnemySpeed, amt);
			e.velocity = dir * speed;
		}

		e.position += e.velocity * elapsed;
	}

	//check if enemies overlaps with goal at the center
	for (auto &e : enemies) {
		glm::vec2 ep = e.position - glm::vec2(0.f, 0.f);
		float len2 = glm::length2(ep);
		if (len2 < (GoalRadius + EnemyRadius) * (GoalRadius + EnemyRadius)) {
			//game over
			game_over = true;
		}
	}

	//circle to line collisions:
	auto RayCircleHelper = [] (glm::vec2 const &ray_start, glm::vec2 const &ray_end, glm::vec2 const &circle_center, float circle_radius) {
		//ray_dir must be normalized
		glm::vec2 ray_dir = glm::normalize(ray_end - ray_start);
		glm::vec2 ray_to_circle = circle_center - ray_start;
		float along = glm::dot(ray_dir, ray_to_circle);
		if (along < 0.0f) return false;
		if (along > glm::length(ray_end - ray_start)) return false;
		float perp2 = glm::length2(ray_to_circle) - along * along;
		if (perp2 > circle_radius * circle_radius) return false;
		return true;
	};

	//check for collisions between enemies and lines:
	//line start at the player 1 position and end at the player 2 position:
	glm::vec2 line_start = players.front().position;
	glm::vec2 line_end =  players.back().position;

	for (auto &e : enemies) {
		if (connected && RayCircleHelper(line_start, line_end, e.position, EnemyRadius)) {
			//collision!
			//kill enemy:
			remove_enemy(&e);
		}
	}

	//update max: 
	if (enemy_killed > enemy_max * 2.f) {
		enemy_max = enemy_killed;
	}
	//spawn the enemy only when both players are present 
	if (players.size() == 2) {
		//spawn the enemy
		if (!game_over && enemies.size() < enemy_max) {
			spawn_enemy();
		}
	}


}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	//will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer


	//send player info helper:
	auto send_player = [&](Player const &player) {
		connection.send(player.position);
		connection.send(player.velocity);
		connection.send(player.color);
	
		//NOTE: can't just 'send(name)' because player.name is not plain-old-data type.
		//effectively: truncates player name to 255 chars
		uint8_t len = uint8_t(std::min< size_t >(255, player.name.size()));
		connection.send(len);
		connection.send_buffer.insert(connection.send_buffer.end(), player.name.begin(), player.name.begin() + len);
	};

	//send enemy info helper:
	auto send_enemy = [&](Enemy const &enemy) {
		connection.send(enemy.position);
		connection.send(enemy.velocity);
		connection.send(enemy.color);
	};

	//player count:
	connection.send(uint8_t(players.size()));
	if (connection_player) send_player(*connection_player);
	for (auto const &player : players) {
		if (&player == connection_player) continue;
		send_player(player);
	}

	//send enemy count:
	connection.send(uint8_t(enemies.size()));
	for (auto const &enemy : enemies) {
		send_enemy(enemy);
	}

	//send if connected
	connection.send(connected);
	//send time
	connection.send(time);
	//send game over
	connection.send(game_over);

	//compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark-3] = uint8_t(size);
	connection.send_buffer[mark-2] = uint8_t(size >> 8);
	connection.send_buffer[mark-1] = uint8_t(size >> 16);
}

bool Game::	recv_state_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 4) return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
	uint32_t size = (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	uint32_t at = 0;
	//expecting complete message:
	if (recv_buffer.size() < 4 + size) return false;

	//copy bytes from buffer and advance position:
	auto read = [&](auto *val) {
		if (at + sizeof(*val) > size) {
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	players.clear();
	uint8_t player_count;
	read(&player_count);
	for (uint8_t i = 0; i < player_count; ++i) {
		players.emplace_back();
		Player &player = players.back();
		read(&player.position);
		read(&player.velocity);
		read(&player.color);
		uint8_t name_len;
		read(&name_len);
		//n.b. would probably be more efficient to directly copy from recv_buffer, but I think this is clearer:
		player.name = "";
		for (uint8_t n = 0; n < name_len; ++n) {
			char c;
			read(&c);
			player.name += c;
		}
	}

	enemies.clear();
	uint8_t enemy_count;
	read(&enemy_count);
	for (uint8_t i = 0; i < enemy_count; ++i) {
		enemies.emplace_back();
		Enemy &enemy = enemies.back();
		read(&enemy.position);
		read(&enemy.velocity);
		read(&enemy.color);
	}

	//receive connected
	read(&connected);

	//receive time
	read(&time);

	//receive game over
	read(&game_over);

	if (at != size) throw std::runtime_error("Trailing data in state message.");

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 4 + size);

	return true;
}
