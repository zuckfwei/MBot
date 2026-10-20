#pragma once

#include <Windows.h>
#include <wincrypt.h>

#include <string>

#pragma comment(lib, "Crypt32.lib")

class Base64
{
public:
	std::string Encode(const unsigned char* bytesToEncode, unsigned int inputLength) const
	{
		if (!bytesToEncode || inputLength == 0)
			return {};

		const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
		DWORD outputLength = 0;

		// Primeiro pedimos o tamanho necessario.
		// Assim a string ja nasce com o espaco correto e evitamos buffers fixos.
		if (!CryptBinaryToStringA(bytesToEncode, inputLength, flags, nullptr, &outputLength))
			return {};

		std::string output(outputLength, '\0');

		// A CryptoAPI do Windows faz a conversao Base64
		if (!CryptBinaryToStringA(bytesToEncode, inputLength, flags, output.data(), &outputLength))
			return {};

		// A funcao pode contar o '\0' final no tamanho retornado.
		// Removemos esse terminador porque std::string ja gerencia o proprio fim da string.
		if (outputLength > 0 && output[outputLength - 1] == '\0')
			--outputLength;

		output.resize(outputLength);
		return output;
	}

	std::string Decode(const std::string& encodedString) const
	{
		if (encodedString.empty())
			return {};

		DWORD outputLength = 0;

		// Primeiro descobrimos quantos bytes o Base64 vai gerar.
		if (!CryptStringToBinaryA(encodedString.c_str(), static_cast<DWORD>(encodedString.size()),
			CRYPT_STRING_BASE64, nullptr, &outputLength, nullptr, nullptr))
			return {};

		std::string output(outputLength, '\0');

		// O resultado decodificado pode conter qualquer byte, inclusive '\0'.
		// Por isso guardamos em std::string com tamanho conhecido, nao em string C.
		if (!CryptStringToBinaryA(encodedString.c_str(), static_cast<DWORD>(encodedString.size()),
			CRYPT_STRING_BASE64, reinterpret_cast<BYTE*>(output.data()), &outputLength, nullptr, nullptr))
			return {};

		output.resize(outputLength);
		return output;
	}
};
