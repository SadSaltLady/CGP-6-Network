#pragma once

#include <glm/glm.hpp>

#include <string>
#include <list>
#include <random>

struct Connection;

//Game state, separate from rendering.

//Currently set up for a "client sends controls" / "server sends whole state" situation.

enum class Message : uint8_t {
	C2S_Controls = 1, //Greg!
	S2C_State = 's',
	//...
};

//used to represent a control input:
struct Button {
	uint8_t downs = 0; //times the button has been pressed
	bool pressed = false; //is the button pressed now
};

//state of one player in the game:
struct Player {
	//player inputs (sent from client):
	struct Controls {
		Button left, right, up, down, jump;

		void send_controls_message(Connection *connection) const;

		//returns 'false' if no message or not a controls message,
		//returns 'true' if read a controls message,
		//throws on malformed controls message
		bool recv_controls_message(Connection *connection);
	} controls;

	//player state (sent from server):
	glm::vec2 position = glm::vec2(0.0f, 0.0f);
	glm::vec2 velocity = glm::vec2(0.0f, 0.0f);

	glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
	std::string name = "";
};

struct Enemy {
	glm::vec2 position = glm::vec2(0.0f, 0.0f);
	glm::vec2 velocity = glm::vec2(0.0f, 0.0f);

	glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
};

struct Game {
	std::list< Player > players; //(using list so they can have stable addresses)
	std::list < Enemy > enemies;
	glm::vec2 alter = glm::vec2(0.0f, 0.0f);
	Player *spawn_player(); //add player the end of the players list (may also, e.g., play some spawn anim)
	Enemy *spawn_enemy(); //add enemy to the end of the enemies list
	void remove_player(Player *); //remove player from game (may also, e.g., play some despawn anim)
	void remove_enemy(Enemy *);

	std::mt19937 mt; //used for spawning players
	uint32_t next_player_number = 1; //used for naming players
	uint32_t next_enemy_number = 1; //used for naming enemies

	//time 
	float time = 0.0f;

	//total enemy count
	uint32_t enemy_max = 3;
	//enemy killed 
	uint32_t enemy_killed = 0;

	//connected or not 
	bool connected = false;

	//over or not
	bool game_over = false;

	Game();

	//state update function:
	void update(float elapsed);

	//constants:
	//the update rate on the server:
	inline static constexpr float Tick = 1.0f / 30.0f;

	//arena size:
	inline static constexpr glm::vec2 ArenaMin = glm::vec2(-0.75f, -1.0f);
	inline static constexpr glm::vec2 ArenaMax = glm::vec2( 0.75f,  1.0f);

	//player constants:
	inline static constexpr float PlayerRadius = 0.06f;
	inline static constexpr float PlayerSpeed = 2.0f;
	inline static constexpr float PlayerAccelHalflife = 0.25f;
	
	//enemy constants:
	inline static constexpr float EnemyRadius = 0.04f;
	inline static constexpr float EnemyAccel = 0.2f;
	inline static constexpr float EnemySpeed = 1.0f;
	inline static constexpr float EnemyAccelHalflife = 0.25f;

	//goal radius
	inline static constexpr float GoalRadius = 0.1f;
	//---- communication helpers ----

	//used by client:
	//set game state from data in connection buffer
	// (return true if data was read)
	bool recv_state_message(Connection *connection);

	//used by server:
	//send game state.
	//  Will move "connection_player" to the front of the front of the sent list.
	void send_state_message(Connection *connection, Player *connection_player = nullptr) const;
};
