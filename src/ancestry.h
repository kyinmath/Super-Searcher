#include <vector>
#include <memory>
#include <chrono>
#include <unordered_set>
#include "globalinfo.h"


//the ancestry tree does keep track of the king.
//note that this must be allocated with new(), because kill_if_useless() uses delete().
struct user_node
{
	bool zombie_node; //true if you're necessary to keep structure, but you're not actually living. necessary if there's no king; in that case, the ancestry tree must have a dead ancestor at the top. also necessary if you're in a join with one parent and two descendants. you can't be killed in that case
	uint64_t id;
	user_node* parent;
	std::chrono::steady_clock::time_point creation_time;
	std::unordered_set<user_node*> descendants;

	user_node(uint64_t i, user_node* p) : id(i), parent(p), creation_time(std::chrono::steady_clock::now()) {}

	void death()
	{
		zombie_node = true;
		kill_if_useless();
	}

	void kill_if_useless() //test if the node is useless (it's dead and has no descendants, or it has one descendant)
	{
		if (!zombie_node) return; //you're living, no need to prune anything
		if (parent)
		{
			if (descendants.size() == 0)
			{
				parent->descendants.erase(this);
				parent->kill_if_useless();
				delete this; //call at the end, so that our parent doesn't go away.
			}
			else if (descendants.size() == 1)
			{
				//move your only descendant to your parent.
				parent->descendants.erase(this);
				parent->descendants.insert(*descendants.begin());
				(*descendants.begin())->parent = parent;
				parent->kill_if_useless();
				delete this;
			}
		}
		else if (parent == nullptr)
		{
			if (descendants.size() == 0)
				error("killing the only guy in the entire tree");
			else if (descendants.size() == 1)
			{
				(*descendants.begin())->parent = nullptr;
				(*descendants.begin())->kill_if_useless();
				delete this;
			}
		}
	}

	//we leave the king inside the ancestry tree. this is useful because sometimes you want to copy him too.
	//but, generally we don't. the king is supposed to be stable, which means he is usually behind the times.
	//on the other hand, having a king inside the tree is nice for rooting the tree. no need to GC a lot of root nodes.

	~user_node()
	{
		//kill_if_useless(this);//this won't work because we call delete. use the kill_if_useless instead; our dtor does nothing.
	}

	std::vector<user_node*> list_all_living(user_node* avoid)
	{
		if (this == avoid) return {};
		std::vector<user_node*> all_nodes;
		if (!zombie_node) all_nodes.push_back(this);
		for (user_node* x : descendants)
		{
			auto k = x->list_all_living(avoid);
			all_nodes.insert(all_nodes.end(), k.begin(), k.end()); //todo: verify that this is right.
		}
		return all_nodes;
	}
	
	//find a user that's far away from a chosen user
	//call this with origin = nullptr.
	user_node* find_far_user(user_node* origin)
	{
		std::vector<user_node*> choices;
		if (parent != nullptr) return parent->find_far_user(this); //trace to the top.
		if (origin == nullptr) //we're trying to find a user that's far away from the king.
		{
			choices = list_all_living(nullptr); // first node will be the king.
		}
		else
		{
			choices = list_all_living(origin);
			//worry: what if there's only one node available, the king himself? if the king has troubles, we'll be boned, because we'll be stuck in a cycle of refreshing the king and crashing. but by now, we've forgotten where the original origin is. so now, 
			if (choices.size() == 1)
				choices = list_all_living(nullptr); //just find some random node. we have a chance to hit the original node, but too bad.
		}
	}
};

extern std::vector<user_node*> name_to_node;
