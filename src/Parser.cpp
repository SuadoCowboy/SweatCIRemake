#include "Parser.h"

#include <algorithm>

#include "PrintCallback.h"

uint64_t ns::maxConsoleVariableCalls = 10000;

void ns::clearStatementData(Context& ctx) {
	ctx.pCommand = nullptr;
	ctx.arguments.clear();
}

void ns::handleCommandCall(Context& ctx, ProgramVariable*& pProgramVar) {
	if (pProgramVar != nullptr) {
		if (ctx.arguments.arguments.size() == 0)
			ns::printf(ns::PrintLevel::ECHO, "Value: {}\n", pProgramVar->get(pProgramVar));
		else
			pProgramVar->set(pProgramVar, ctx.arguments.arguments[0]);

		clearStatementData(ctx);
		return;
	}

	if (ctx.pCommand == nullptr)
		return;

	if (ctx.pCommand->minArgs > ctx.arguments.arguments.size()) {
		if (ctx.pCommand->minArgs == ctx.pCommand->maxArgs)
			ns::printf(ns::PrintLevel::ERROR, "Expected {} argument(s) but received {} arguments\n", static_cast<uint16_t>(ctx.pCommand->minArgs), ctx.arguments.arguments.size());
		else
			ns::printf(ns::PrintLevel::ERROR, "Expected arguments between [{}, {}] but received {} arguments\n", static_cast<uint16_t>(ctx.pCommand->minArgs), static_cast<uint16_t>(ctx.pCommand->maxArgs), ctx.arguments.arguments.size());

		ns::printf(ns::PrintLevel::ECHO, "{} {}\n", ctx.pCommand->name, ctx.pCommand->getArgumentsNames());
		clearStatementData(ctx);
		return;
	}

	switch (ctx.pCommand->name[0]) {
	case NIKISCRIPT_TOGGLE_ON: {
		auto it = std::find(ctx.toggleCommandsRunning.begin(), ctx.toggleCommandsRunning.end(), ctx.pCommand);

		if (it == ctx.toggleCommandsRunning.end())
			ctx.toggleCommandsRunning.push_back(ctx.pCommand);

		else {
			clearStatementData(ctx);
			return;
		}

		break;
	}
	
	case NIKISCRIPT_TOGGLE_OFF: {
		Command* pPlusCommand = ctx.commands.get('+'+std::string(ctx.pCommand->name.substr(1)));
		if (pPlusCommand == nullptr)
			break;

		auto it = std::find(ctx.toggleCommandsRunning.begin(), ctx.toggleCommandsRunning.end(), pPlusCommand);

		if (it == ctx.toggleCommandsRunning.end()) {
			clearStatementData(ctx);
			return;

		} else
			ctx.toggleCommandsRunning.erase(it);

		break;
	}
	}

	ctx.pCommand->callback(ctx);
	clearStatementData(ctx);
}

void ns::handleArgumentToken(Context& ctx) {
	insertReferencesInToken(ctx, ctx.pLexer->token);

	if (ctx.pCommand == nullptr) { // if command is nullptr then just append arguments to a single one. This is useful for ProgramVariable
		if (ctx.arguments.arguments.size() == 0)
			ctx.arguments.arguments.push_back(ctx.pLexer->token.value);
		else
			ctx.arguments.arguments.back() += ' '+ctx.pLexer->token.value;
		return;
	}

	if (ctx.pCommand->maxArgs == 0) {
		ns::printf(ns::PrintLevel::ERROR, "Expected 0 arguments for {} command\n", ctx.pCommand->name);
		clearStatementData(ctx);
		ctx.pLexer->advanceUntil(static_cast<uint8_t>(TokenType::EOS));
		return;
	}

	if (ctx.arguments.arguments.size() == ctx.pCommand->maxArgs) {
		ctx.pLexer->token.value = ctx.arguments.arguments.back()+' '+ctx.pLexer->token.value;
		ctx.arguments.arguments.pop_back();
	}

	const std::string_view& arg = ctx.pCommand->argsDescriptions[ctx.arguments.arguments.size()*2];
	switch (arg[0]) {
	case 'i':
		try {
			std::stoi(ctx.pLexer->token.value);
		} catch (...) {
			ns::printf(PrintLevel::ERROR, "{} -> Type not matched: expected (i)nteger number\n", arg);
			clearStatementData(ctx);
			ctx.pLexer->advanceUntil(static_cast<uint8_t>(TokenType::EOS));
		}
		break;

	case 'd':
		try {
			std::stof(ctx.pLexer->token.value);
		} catch (...) {
			ns::printf(PrintLevel::ERROR, "{} -> Type not matched: expected (d)ecimal number\n", arg);
			clearStatementData(ctx);
			ctx.pLexer->advanceUntil(static_cast<uint8_t>(TokenType::EOS));
		}
		break;

	case 's':
		break;

	case 'v':
		if (ctx.programVariables.count(ctx.pLexer->token.value) == 0 && ctx.consoleVariables.count(ctx.pLexer->token.value) == 0) {
			ns::printf(PrintLevel::ERROR, "{} -> Type not matched: expected (v)ariable\n", arg);
			clearStatementData(ctx);
			ctx.pLexer->advanceUntil(static_cast<uint8_t>(TokenType::EOS));
		}
		break;

	default: // should never happen
		break;
	}

	ctx.arguments.arguments.push_back(ctx.pLexer->token.value);
}

void ns::handleConsoleVariableCall(Context& ctx, ProgramVariable*& pProgramVar) {
	Lexer* pOriginalLexer = ctx.pLexer;

	std::vector<Lexer> tempLexers;
	tempLexers.emplace_back(ctx.consoleVariables[ctx.pLexer->token.value]);

	ctx.pLexer = &tempLexers.back();
	ctx.pLexer->advance();

	//ctx.runningFrom |= VARIABLE;

	while (tempLexers.size() != 0) {
		if (ctx.pLexer->token.type == TokenType::IDENTIFIER) {
			if (ctx.consoleVariables.count(ctx.pLexer->token.value) != 0) {
				if (maxConsoleVariableCalls != 0 && tempLexers.size() >= maxConsoleVariableCalls) {
					ctx.pLexer->advanceUntil(static_cast<uint8_t>(TokenType::EOS));
				} else {
					tempLexers.emplace_back(ctx.consoleVariables[ctx.pLexer->token.value]);
					ctx.pLexer = &tempLexers.back();
				}

			} else {
				ctx.pCommand = ctx.commands.get(ctx.pLexer->token.value);

				if (ctx.pCommand == nullptr) {
					ns::printf(PrintLevel::ERROR, "Unknown identifier \"{}\"\n", ctx.pLexer->token.value);
					ctx.pLexer->advanceUntil(static_cast<uint8_t>(TokenType::EOS));
				}
			}
		}

		switch (ctx.pLexer->token.type) {
		case TokenType::EOS:
			handleCommandCall(ctx, pProgramVar);
			break;

		case TokenType::ARGUMENT:
			handleArgumentToken(ctx);
			break;

		default:
			break;
		}

		ctx.pLexer->advance();
		while (ctx.pLexer->token.type == TokenType::END) {
			handleCommandCall(ctx, pProgramVar);

			tempLexers.pop_back();
			if (tempLexers.size() == 0)
				break;

			ctx.pLexer = &tempLexers.back();
			ctx.pLexer->advanceUntil(static_cast<uint8_t>(TokenType::EOS));
		}
	}

	//ctx.runningFrom &= ~VARIABLE;
	ctx.pLexer = pOriginalLexer;
}

void ns::updateLoopVariables(ns::Context& ctx) {
	//ctx.runningFrom |= VARIABLE|VARIABLE_LOOP;

	ctx.pLexer->clear();
	for (auto& pVar : ctx.loopVariablesRunning) {
		ctx.pLexer->input = pVar->second;
		parse(ctx);
		ctx.pLexer->clear();
	}
	
	//ctx.runningFrom &= ~VARIABLE;
	//ctx.runningFrom &= ~VARIABLE_LOOP;
}

void ns::parse(Context& ctx) {
	if (ctx.pLexer == nullptr)
		return;

	ProgramVariable* pProgramVar = nullptr;

	ctx.pLexer->advance();
	while (ctx.pLexer->token.type != TokenType::END) {
		switch (ctx.pLexer->token.type) {
		case TokenType::IDENTIFIER: // can be either variable or command
			if (ctx.consoleVariables.count(ctx.pLexer->token.value) != 0) {
				switch (ctx.pLexer->token.value[0]) {
				case NIKISCRIPT_TOGGLE_ON: {
					ConsoleVariables::pointer pVarPair = &*ctx.consoleVariables.find(ctx.pLexer->token.value);
					auto it = std::find(ctx.toggleVariablesRunning.begin(), ctx.toggleVariablesRunning.end(), pVarPair);

					if (it == ctx.toggleVariablesRunning.end()) {
						ctx.toggleVariablesRunning.push_back(pVarPair);
						handleConsoleVariableCall(ctx, pProgramVar);
					}

					break;
				}

				case NIKISCRIPT_TOGGLE_OFF: {
					ConsoleVariables::pointer pPlusVariable = nullptr;
					{
						auto it = ctx.consoleVariables.find('+'+ctx.pLexer->token.value.substr(1));
						if (it == ctx.consoleVariables.end())
							break;
	
						pPlusVariable = &*it;
					}

					auto it = std::find(ctx.toggleVariablesRunning.begin(), ctx.toggleVariablesRunning.end(), pPlusVariable);
					if (it != ctx.toggleVariablesRunning.end()) {
						ctx.toggleVariablesRunning.erase(it);
						handleConsoleVariableCall(ctx, pProgramVar);
					}
					break;
				}

				case NIKISCRIPT_LOOP_VARIABLE: {
					ConsoleVariables::pointer pVar = &*ctx.consoleVariables.find(ctx.pLexer->token.value);

					auto it = std::find(ctx.loopVariablesRunning.begin(), ctx.loopVariablesRunning.end(), pVar);
					if (it == ctx.loopVariablesRunning.end())
						ctx.loopVariablesRunning.push_back(pVar);
					else
						ctx.loopVariablesRunning.erase(it);
					break;
				}

				default:
					handleConsoleVariableCall(ctx, pProgramVar);
				}

				ctx.pLexer->advanceUntil(static_cast<uint8_t>(TokenType::EOS));

			} else if (ctx.programVariables.count(ctx.pLexer->token.value) != 0) {
				pProgramVar = &ctx.programVariables[ctx.pLexer->token.value];
				ctx.pLexer->advance();

			} else {
				ctx.pCommand = ctx.commands.get(ctx.pLexer->token.value);
				if (ctx.pCommand == nullptr) {
					ns::printf(PrintLevel::ERROR, "Unknown identifier \"{}\"\n", ctx.pLexer->token.value);
					ctx.pLexer->advanceUntil(static_cast<uint8_t>(TokenType::EOS));
				} else
					ctx.pLexer->advance();
			}

			break;

		case TokenType::ARGUMENT:
			handleArgumentToken(ctx);
			ctx.pLexer->advance();
			break;

		case TokenType::EOS:
			handleCommandCall(ctx, pProgramVar);
			ctx.pLexer->advance();
			break;

		default:
			ctx.pLexer->advance();
			break;
		}
	}

	handleCommandCall(ctx, pProgramVar);
}